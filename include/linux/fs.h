/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_FS_H
#define _LINUX_FS_H

#include <linux/linkage.h>
#include <linux/wait_bit.h>
#include <linux/kdev_t.h>
#include <linux/dcache.h>
#include <linux/path.h>
#include <linux/stat.h>
#include <linux/cache.h>
#include <linux/list.h>
#include <linux/list_lru.h>
#include <linux/llist.h>
#include <linux/radix-tree.h>
#include <linux/xarray.h>
#include <linux/rbtree.h>
#include <linux/init.h>
#include <linux/pid.h>
#include <linux/bug.h>
#include <linux/mutex.h>
#include <linux/rwsem.h>
#include <linux/mm_types.h>
#include <linux/capability.h>
#include <linux/semaphore.h>
#include <linux/fcntl.h>
#include <linux/fiemap.h>
#include <linux/rculist_bl.h>
#include <linux/atomic.h>
#include <linux/shrinker.h>
#include <linux/migrate_mode.h>
#include <linux/uidgid.h>
#include <linux/lockdep.h>
#include <linux/percpu-rwsem.h>
#include <linux/workqueue.h>
#include <linux/delayed_call.h>
#include <linux/uuid.h>
#include <linux/errseq.h>
#include <linux/ioprio.h>
#include <linux/fs_types.h>
#include <linux/build_bug.h>
#include <linux/stddef.h>

#include <asm/byteorder.h>
#include <uapi/linux/fs.h>

struct backing_dev_info;
struct bdi_writeback;
struct bio;
struct export_operations;
struct hd_geometry;
struct iovec;
struct kiocb;
struct kobject;
struct pipe_iyesde_info;
struct poll_table_struct;
struct kstatfs;
struct vm_area_struct;
struct vfsmount;
struct cred;
struct swap_info_struct;
struct seq_file;
struct workqueue_struct;
struct iov_iter;
struct fscrypt_info;
struct fscrypt_operations;
struct fsverity_info;
struct fsverity_operations;
struct fs_context;
struct fs_parameter_description;

extern void __init iyesde_init(void);
extern void __init iyesde_init_early(void);
extern void __init files_init(void);
extern void __init files_maxfiles_init(void);

extern struct files_stat_struct files_stat;
extern unsigned long get_max_files(void);
extern unsigned int sysctl_nr_open;
extern struct iyesdes_stat_t iyesdes_stat;
extern int leases_enable, lease_break_time;
extern int sysctl_protected_symlinks;
extern int sysctl_protected_hardlinks;
extern int sysctl_protected_fifos;
extern int sysctl_protected_regular;

typedef __kernel_rwf_t rwf_t;

struct buffer_head;
typedef int (get_block_t)(struct iyesde *iyesde, sector_t iblock,
			struct buffer_head *bh_result, int create);
typedef int (dio_iodone_t)(struct kiocb *iocb, loff_t offset,
			ssize_t bytes, void *private);

#define MAY_EXEC		0x00000001
#define MAY_WRITE		0x00000002
#define MAY_READ		0x00000004
#define MAY_APPEND		0x00000008
#define MAY_ACCESS		0x00000010
#define MAY_OPEN		0x00000020
#define MAY_CHDIR		0x00000040
/* called from RCU mode, don't block */
#define MAY_NOT_BLOCK		0x00000080

/*
 * flags in file.f_mode.  Note that FMODE_READ and FMODE_WRITE must correspond
 * to O_WRONLY and O_RDWR via the strange trick in do_dentry_open()
 */

/* file is open for reading */
#define FMODE_READ		((__force fmode_t)0x1)
/* file is open for writing */
#define FMODE_WRITE		((__force fmode_t)0x2)
/* file is seekable */
#define FMODE_LSEEK		((__force fmode_t)0x4)
/* file can be accessed using pread */
#define FMODE_PREAD		((__force fmode_t)0x8)
/* file can be accessed using pwrite */
#define FMODE_PWRITE		((__force fmode_t)0x10)
/* File is opened for execution with sys_execve / sys_uselib */
#define FMODE_EXEC		((__force fmode_t)0x20)
/* File is opened with O_NDELAY (only set for block devices) */
#define FMODE_NDELAY		((__force fmode_t)0x40)
/* File is opened with O_EXCL (only set for block devices) */
#define FMODE_EXCL		((__force fmode_t)0x80)
/* File is opened using open(.., 3, ..) and is writeable only for ioctls
   (specialy hack for floppy.c) */
#define FMODE_WRITE_IOCTL	((__force fmode_t)0x100)
/* 32bit hashes as llseek() offset (for directories) */
#define FMODE_32BITHASH         ((__force fmode_t)0x200)
/* 64bit hashes as llseek() offset (for directories) */
#define FMODE_64BITHASH         ((__force fmode_t)0x400)

/*
 * Don't update ctime and mtime.
 *
 * Currently a special hack for the XFS open_by_handle ioctl, but we'll
 * hopefully graduate it to a proper O_CMTIME flag supported by open(2) soon.
 */
#define FMODE_NOCMTIME		((__force fmode_t)0x800)

/* Expect random access pattern */
#define FMODE_RANDOM		((__force fmode_t)0x1000)

/* File is huge (eg. /dev/kmem): treat loff_t as unsigned */
#define FMODE_UNSIGNED_OFFSET	((__force fmode_t)0x2000)

/* File is opened with O_PATH; almost yesthing can be done with it */
#define FMODE_PATH		((__force fmode_t)0x4000)

/* File needs atomic accesses to f_pos */
#define FMODE_ATOMIC_POS	((__force fmode_t)0x8000)
/* Write access to underlying fs */
#define FMODE_WRITER		((__force fmode_t)0x10000)
/* Has read method(s) */
#define FMODE_CAN_READ          ((__force fmode_t)0x20000)
/* Has write method(s) */
#define FMODE_CAN_WRITE         ((__force fmode_t)0x40000)

#define FMODE_OPENED		((__force fmode_t)0x80000)
#define FMODE_CREATED		((__force fmode_t)0x100000)

/* File is stream-like */
#define FMODE_STREAM		((__force fmode_t)0x200000)

/* File was opened by fayestify and shouldn't generate fayestify events */
#define FMODE_NONOTIFY		((__force fmode_t)0x4000000)

/* File is capable of returning -EAGAIN if I/O will block */
#define FMODE_NOWAIT		((__force fmode_t)0x8000000)

/* File represents mount that needs unmounting */
#define FMODE_NEED_UNMOUNT	((__force fmode_t)0x10000000)

/* File does yest contribute to nr_files count */
#define FMODE_NOACCOUNT		((__force fmode_t)0x20000000)

/*
 * Flag for rw_copy_check_uvector and compat_rw_copy_check_uvector
 * that indicates that they should check the contents of the iovec are
 * valid, but yest check the memory that the iovec elements
 * points too.
 */
#define CHECK_IOVEC_ONLY -1

/*
 * Attribute flags.  These should be or-ed together to figure out what
 * has been changed!
 */
#define ATTR_MODE	(1 << 0)
#define ATTR_UID	(1 << 1)
#define ATTR_GID	(1 << 2)
#define ATTR_SIZE	(1 << 3)
#define ATTR_ATIME	(1 << 4)
#define ATTR_MTIME	(1 << 5)
#define ATTR_CTIME	(1 << 6)
#define ATTR_ATIME_SET	(1 << 7)
#define ATTR_MTIME_SET	(1 << 8)
#define ATTR_FORCE	(1 << 9) /* Not a change, but a change it */
#define ATTR_KILL_SUID	(1 << 11)
#define ATTR_KILL_SGID	(1 << 12)
#define ATTR_FILE	(1 << 13)
#define ATTR_KILL_PRIV	(1 << 14)
#define ATTR_OPEN	(1 << 15) /* Truncating from open(O_TRUNC) */
#define ATTR_TIMES_SET	(1 << 16)
#define ATTR_TOUCH	(1 << 17)

/*
 * Whiteout is represented by a char device.  The following constants define the
 * mode and device number to use.
 */
#define WHITEOUT_MODE 0
#define WHITEOUT_DEV 0

/*
 * This is the Iyesde Attributes structure, used for yestify_change().  It
 * uses the above definitions as flags, to kyesw which values have changed.
 * Also, in this manner, a Filesystem can look at only the values it cares
 * about.  Basically, these are the attributes that the VFS layer can
 * request to change from the FS layer.
 *
 * Derek Atkins <warlord@MIT.EDU> 94-10-20
 */
struct iattr {
	unsigned int	ia_valid;
	umode_t		ia_mode;
	kuid_t		ia_uid;
	kgid_t		ia_gid;
	loff_t		ia_size;
	struct timespec64 ia_atime;
	struct timespec64 ia_mtime;
	struct timespec64 ia_ctime;

	/*
	 * Not an attribute, but an auxiliary info for filesystems wanting to
	 * implement an ftruncate() like method.  NOTE: filesystem should
	 * check for (ia_valid & ATTR_FILE), and yest for (ia_file != NULL).
	 */
	struct file	*ia_file;
};

/*
 * Includes for diskquotas.
 */
#include <linux/quota.h>

/*
 * Maximum number of layers of fs stack.  Needs to be limited to
 * prevent kernel stack overflow
 */
#define FILESYSTEM_MAX_STACK_DEPTH 2

/** 
 * enum positive_aop_returns - aop return codes with specific semantics
 *
 * @AOP_WRITEPAGE_ACTIVATE: Informs the caller that page writeback has
 * 			    completed, that the page is still locked, and
 * 			    should be considered active.  The VM uses this hint
 * 			    to return the page to the active list -- it won't
 * 			    be a candidate for writeback again in the near
 * 			    future.  Other callers must be careful to unlock
 * 			    the page if they get this return.  Returned by
 * 			    writepage(); 
 *
 * @AOP_TRUNCATED_PAGE: The AOP method that was handed a locked page has
 *  			unlocked it and the page might have been truncated.
 *  			The caller should back up to acquiring a new page and
 *  			trying again.  The aop will be taking reasonable
 *  			precautions yest to livelock.  If the caller held a page
 *  			reference, it should drop it before retrying.  Returned
 *  			by readpage().
 *
 * address_space_operation functions return these large constants to indicate
 * special semantics to the caller.  These are much larger than the bytes in a
 * page to allow for functions that return the number of bytes operated on in a
 * given page.
 */

enum positive_aop_returns {
	AOP_WRITEPAGE_ACTIVATE	= 0x80000,
	AOP_TRUNCATED_PAGE	= 0x80001,
};

#define AOP_FLAG_CONT_EXPAND		0x0001 /* called from cont_expand */
#define AOP_FLAG_NOFS			0x0002 /* used by filesystem to direct
						* helper code (eg buffer layer)
						* to clear GFP_FS from alloc */

/*
 * oh the beauties of C type declarations.
 */
struct page;
struct address_space;
struct writeback_control;

/*
 * Write life time hint values.
 * Stored in struct iyesde as u8.
 */
enum rw_hint {
	WRITE_LIFE_NOT_SET	= 0,
	WRITE_LIFE_NONE		= RWH_WRITE_LIFE_NONE,
	WRITE_LIFE_SHORT	= RWH_WRITE_LIFE_SHORT,
	WRITE_LIFE_MEDIUM	= RWH_WRITE_LIFE_MEDIUM,
	WRITE_LIFE_LONG		= RWH_WRITE_LIFE_LONG,
	WRITE_LIFE_EXTREME	= RWH_WRITE_LIFE_EXTREME,
};

#define IOCB_EVENTFD		(1 << 0)
#define IOCB_APPEND		(1 << 1)
#define IOCB_DIRECT		(1 << 2)
#define IOCB_HIPRI		(1 << 3)
#define IOCB_DSYNC		(1 << 4)
#define IOCB_SYNC		(1 << 5)
#define IOCB_WRITE		(1 << 6)
#define IOCB_NOWAIT		(1 << 7)

struct kiocb {
	struct file		*ki_filp;

	/* The 'ki_filp' pointer is shared in a union for aio */
	randomized_struct_fields_start

	loff_t			ki_pos;
	void (*ki_complete)(struct kiocb *iocb, long ret, long ret2);
	void			*private;
	int			ki_flags;
	u16			ki_hint;
	u16			ki_ioprio; /* See linux/ioprio.h */
	unsigned int		ki_cookie; /* for ->iopoll */

	randomized_struct_fields_end
};

static inline bool is_sync_kiocb(struct kiocb *kiocb)
{
	return kiocb->ki_complete == NULL;
}

/*
 * "descriptor" for what we're up to with a read.
 * This allows us to use the same read code yet
 * have multiple different users of the data that
 * we read from a file.
 *
 * The simplest case just copies the data to user
 * mode.
 */
typedef struct {
	size_t written;
	size_t count;
	union {
		char __user *buf;
		void *data;
	} arg;
	int error;
} read_descriptor_t;

typedef int (*read_actor_t)(read_descriptor_t *, struct page *,
		unsigned long, unsigned long);

struct address_space_operations {
	int (*writepage)(struct page *page, struct writeback_control *wbc);
	int (*readpage)(struct file *, struct page *);

	/* Write back some dirty pages from this mapping. */
	int (*writepages)(struct address_space *, struct writeback_control *);

	/* Set a page dirty.  Return true if this dirtied it */
	int (*set_page_dirty)(struct page *page);

	/*
	 * Reads in the requested pages. Unlike ->readpage(), this is
	 * PURELY used for read-ahead!.
	 */
	int (*readpages)(struct file *filp, struct address_space *mapping,
			struct list_head *pages, unsigned nr_pages);

	int (*write_begin)(struct file *, struct address_space *mapping,
				loff_t pos, unsigned len, unsigned flags,
				struct page **pagep, void **fsdata);
	int (*write_end)(struct file *, struct address_space *mapping,
				loff_t pos, unsigned len, unsigned copied,
				struct page *page, void *fsdata);

	/* Unfortunately this kludge is needed for FIBMAP. Don't use it */
	sector_t (*bmap)(struct address_space *, sector_t);
	void (*invalidatepage) (struct page *, unsigned int, unsigned int);
	int (*releasepage) (struct page *, gfp_t);
	void (*freepage)(struct page *);
	ssize_t (*direct_IO)(struct kiocb *, struct iov_iter *iter);
	/*
	 * migrate the contents of a page to the specified target. If
	 * migrate_mode is MIGRATE_ASYNC, it must yest block.
	 */
	int (*migratepage) (struct address_space *,
			struct page *, struct page *, enum migrate_mode);
	bool (*isolate_page)(struct page *, isolate_mode_t);
	void (*putback_page)(struct page *);
	int (*launder_page) (struct page *);
	int (*is_partially_uptodate) (struct page *, unsigned long,
					unsigned long);
	void (*is_dirty_writeback) (struct page *, bool *, bool *);
	int (*error_remove_page)(struct address_space *, struct page *);

	/* swapfile support */
	int (*swap_activate)(struct swap_info_struct *sis, struct file *file,
				sector_t *span);
	void (*swap_deactivate)(struct file *file);
};

extern const struct address_space_operations empty_aops;

/*
 * pagecache_write_begin/pagecache_write_end must be used by general code
 * to write into the pagecache.
 */
int pagecache_write_begin(struct file *, struct address_space *mapping,
				loff_t pos, unsigned len, unsigned flags,
				struct page **pagep, void **fsdata);

int pagecache_write_end(struct file *, struct address_space *mapping,
				loff_t pos, unsigned len, unsigned copied,
				struct page *page, void *fsdata);

/**
 * struct address_space - Contents of a cacheable, mappable object.
 * @host: Owner, either the iyesde or the block_device.
 * @i_pages: Cached pages.
 * @gfp_mask: Memory allocation flags to use for allocating pages.
 * @i_mmap_writable: Number of VM_SHARED mappings.
 * @nr_thps: Number of THPs in the pagecache (yesn-shmem only).
 * @i_mmap: Tree of private and shared mappings.
 * @i_mmap_rwsem: Protects @i_mmap and @i_mmap_writable.
 * @nrpages: Number of page entries, protected by the i_pages lock.
 * @nrexceptional: Shadow or DAX entries, protected by the i_pages lock.
 * @writeback_index: Writeback starts here.
 * @a_ops: Methods.
 * @flags: Error bits and flags (AS_*).
 * @wb_err: The most recent error which has occurred.
 * @private_lock: For use by the owner of the address_space.
 * @private_list: For use by the owner of the address_space.
 * @private_data: For use by the owner of the address_space.
 */
struct address_space {
	struct iyesde		*host;
	struct xarray		i_pages;
	gfp_t			gfp_mask;
	atomic_t		i_mmap_writable;
#ifdef CONFIG_READ_ONLY_THP_FOR_FS
	/* number of thp, only for yesn-shmem files */
	atomic_t		nr_thps;
#endif
	struct rb_root_cached	i_mmap;
	struct rw_semaphore	i_mmap_rwsem;
	unsigned long		nrpages;
	unsigned long		nrexceptional;
	pgoff_t			writeback_index;
	const struct address_space_operations *a_ops;
	unsigned long		flags;
	errseq_t		wb_err;
	spinlock_t		private_lock;
	struct list_head	private_list;
	void			*private_data;
} __attribute__((aligned(sizeof(long)))) __randomize_layout;
	/*
	 * On most architectures that alignment is already the case; but
	 * must be enforced here for CRIS, to let the least significant bit
	 * of struct page's "mapping" pointer be used for PAGE_MAPPING_ANON.
	 */
struct request_queue;

struct block_device {
	dev_t			bd_dev;  /* yest a kdev_t - it's a search key */
	int			bd_openers;
	struct iyesde *		bd_iyesde;	/* will die */
	struct super_block *	bd_super;
	struct mutex		bd_mutex;	/* open/close mutex */
	void *			bd_claiming;
	void *			bd_holder;
	int			bd_holders;
	bool			bd_write_holder;
#ifdef CONFIG_SYSFS
	struct list_head	bd_holder_disks;
#endif
	struct block_device *	bd_contains;
	unsigned		bd_block_size;
	u8			bd_partyes;
	struct hd_struct *	bd_part;
	/* number of times partitions within this device have been opened. */
	unsigned		bd_part_count;
	int			bd_invalidated;
	struct gendisk *	bd_disk;
	struct request_queue *  bd_queue;
	struct backing_dev_info *bd_bdi;
	struct list_head	bd_list;
	/*
	 * Private data.  You must have bd_claim'ed the block_device
	 * to use this.  NOTE:  bd_claim allows an owner to claim
	 * the same device multiple times, the owner must take special
	 * care to yest mess up bd_private for that case.
	 */
	unsigned long		bd_private;

	/* The counter of freeze processes */
	int			bd_fsfreeze_count;
	/* Mutex for freeze */
	struct mutex		bd_fsfreeze_mutex;
} __randomize_layout;

/* XArray tags, for tagging dirty and writeback pages in the pagecache. */
#define PAGECACHE_TAG_DIRTY	XA_MARK_0
#define PAGECACHE_TAG_WRITEBACK	XA_MARK_1
#define PAGECACHE_TAG_TOWRITE	XA_MARK_2

/*
 * Returns true if any of the pages in the mapping are marked with the tag.
 */
static inline bool mapping_tagged(struct address_space *mapping, xa_mark_t tag)
{
	return xa_marked(&mapping->i_pages, tag);
}

static inline void i_mmap_lock_write(struct address_space *mapping)
{
	down_write(&mapping->i_mmap_rwsem);
}

static inline void i_mmap_unlock_write(struct address_space *mapping)
{
	up_write(&mapping->i_mmap_rwsem);
}

static inline void i_mmap_lock_read(struct address_space *mapping)
{
	down_read(&mapping->i_mmap_rwsem);
}

static inline void i_mmap_unlock_read(struct address_space *mapping)
{
	up_read(&mapping->i_mmap_rwsem);
}

/*
 * Might pages of this file be mapped into userspace?
 */
static inline int mapping_mapped(struct address_space *mapping)
{
	return	!RB_EMPTY_ROOT(&mapping->i_mmap.rb_root);
}

/*
 * Might pages of this file have been modified in userspace?
 * Note that i_mmap_writable counts all VM_SHARED vmas: do_mmap_pgoff
 * marks vma as VM_SHARED if it is shared, and the file was opened for
 * writing i.e. vma may be mprotected writable even if yesw readonly.
 *
 * If i_mmap_writable is negative, yes new writable mappings are allowed. You
 * can only deny writable mappings, if yesne exists right yesw.
 */
static inline int mapping_writably_mapped(struct address_space *mapping)
{
	return atomic_read(&mapping->i_mmap_writable) > 0;
}

static inline int mapping_map_writable(struct address_space *mapping)
{
	return atomic_inc_unless_negative(&mapping->i_mmap_writable) ?
		0 : -EPERM;
}

static inline void mapping_unmap_writable(struct address_space *mapping)
{
	atomic_dec(&mapping->i_mmap_writable);
}

static inline int mapping_deny_writable(struct address_space *mapping)
{
	return atomic_dec_unless_positive(&mapping->i_mmap_writable) ?
		0 : -EBUSY;
}

static inline void mapping_allow_writable(struct address_space *mapping)
{
	atomic_inc(&mapping->i_mmap_writable);
}

/*
 * Use sequence counter to get consistent i_size on 32-bit processors.
 */
#if BITS_PER_LONG==32 && defined(CONFIG_SMP)
#include <linux/seqlock.h>
#define __NEED_I_SIZE_ORDERED
#define i_size_ordered_init(iyesde) seqcount_init(&iyesde->i_size_seqcount)
#else
#define i_size_ordered_init(iyesde) do { } while (0)
#endif

struct posix_acl;
#define ACL_NOT_CACHED ((void *)(-1))
#define ACL_DONT_CACHE ((void *)(-3))

static inline struct posix_acl *
uncached_acl_sentinel(struct task_struct *task)
{
	return (void *)task + 1;
}

static inline bool
is_uncached_acl(struct posix_acl *acl)
{
	return (long)acl & 1;
}

#define IOP_FASTPERM	0x0001
#define IOP_LOOKUP	0x0002
#define IOP_NOFOLLOW	0x0004
#define IOP_XATTR	0x0008
#define IOP_DEFAULT_READLINK	0x0010

struct fsyestify_mark_connector;

/*
 * Keep mostly read-only and often accessed (especially for
 * the RCU path lookup and 'stat' data) fields at the beginning
 * of the 'struct iyesde'
 */
struct iyesde {
	umode_t			i_mode;
	unsigned short		i_opflags;
	kuid_t			i_uid;
	kgid_t			i_gid;
	unsigned int		i_flags;

#ifdef CONFIG_FS_POSIX_ACL
	struct posix_acl	*i_acl;
	struct posix_acl	*i_default_acl;
#endif

	const struct iyesde_operations	*i_op;
	struct super_block	*i_sb;
	struct address_space	*i_mapping;

#ifdef CONFIG_SECURITY
	void			*i_security;
#endif

	/* Stat data, yest accessed from path walking */
	unsigned long		i_iyes;
	/*
	 * Filesystems may only read i_nlink directly.  They shall use the
	 * following functions for modification:
	 *
	 *    (set|clear|inc|drop)_nlink
	 *    iyesde_(inc|dec)_link_count
	 */
	union {
		const unsigned int i_nlink;
		unsigned int __i_nlink;
	};
	dev_t			i_rdev;
	loff_t			i_size;
	struct timespec64	i_atime;
	struct timespec64	i_mtime;
	struct timespec64	i_ctime;
	spinlock_t		i_lock;	/* i_blocks, i_bytes, maybe i_size */
	unsigned short          i_bytes;
	u8			i_blkbits;
	u8			i_write_hint;
	blkcnt_t		i_blocks;

#ifdef __NEED_I_SIZE_ORDERED
	seqcount_t		i_size_seqcount;
#endif

	/* Misc */
	unsigned long		i_state;
	struct rw_semaphore	i_rwsem;

	unsigned long		dirtied_when;	/* jiffies of first dirtying */
	unsigned long		dirtied_time_when;

	struct hlist_yesde	i_hash;
	struct list_head	i_io_list;	/* backing dev IO list */
#ifdef CONFIG_CGROUP_WRITEBACK
	struct bdi_writeback	*i_wb;		/* the associated cgroup wb */

	/* foreign iyesde detection, see wbc_detach_iyesde() */
	int			i_wb_frn_winner;
	u16			i_wb_frn_avg_time;
	u16			i_wb_frn_history;
#endif
	struct list_head	i_lru;		/* iyesde LRU list */
	struct list_head	i_sb_list;
	struct list_head	i_wb_list;	/* backing dev writeback list */
	union {
		struct hlist_head	i_dentry;
		struct rcu_head		i_rcu;
	};
	atomic64_t		i_version;
	atomic_t		i_count;
	atomic_t		i_dio_count;
	atomic_t		i_writecount;
#if defined(CONFIG_IMA) || defined(CONFIG_FILE_LOCKING)
	atomic_t		i_readcount; /* struct files open RO */
#endif
	union {
		const struct file_operations	*i_fop;	/* former ->i_op->default_file_ops */
		void (*free_iyesde)(struct iyesde *);
	};
	struct file_lock_context	*i_flctx;
	struct address_space	i_data;
	struct list_head	i_devices;
	union {
		struct pipe_iyesde_info	*i_pipe;
		struct block_device	*i_bdev;
		struct cdev		*i_cdev;
		char			*i_link;
		unsigned		i_dir_seq;
	};

	__u32			i_generation;

#ifdef CONFIG_FSNOTIFY
	__u32			i_fsyestify_mask; /* all events this iyesde cares about */
	struct fsyestify_mark_connector __rcu	*i_fsyestify_marks;
#endif

#ifdef CONFIG_FS_ENCRYPTION
	struct fscrypt_info	*i_crypt_info;
#endif

#ifdef CONFIG_FS_VERITY
	struct fsverity_info	*i_verity_info;
#endif

	void			*i_private; /* fs or device private pointer */
} __randomize_layout;

struct timespec64 timestamp_truncate(struct timespec64 t, struct iyesde *iyesde);

static inline unsigned int i_blocksize(const struct iyesde *yesde)
{
	return (1 << yesde->i_blkbits);
}

static inline int iyesde_unhashed(struct iyesde *iyesde)
{
	return hlist_unhashed(&iyesde->i_hash);
}

/*
 * __mark_iyesde_dirty expects iyesdes to be hashed.  Since we don't
 * want special iyesdes in the fileset iyesde space, we make them
 * appear hashed, but do yest put on any lists.  hlist_del()
 * will work fine and require yes locking.
 */
static inline void iyesde_fake_hash(struct iyesde *iyesde)
{
	hlist_add_fake(&iyesde->i_hash);
}

/*
 * iyesde->i_mutex nesting subclasses for the lock validator:
 *
 * 0: the object of the current VFS operation
 * 1: parent
 * 2: child/target
 * 3: xattr
 * 4: second yesn-directory
 * 5: second parent (when locking independent directories in rename)
 *
 * I_MUTEX_NONDIR2 is for certain operations (such as rename) which lock two
 * yesn-directories at once.
 *
 * The locking order between these classes is
 * parent[2] -> child -> grandchild -> yesrmal -> xattr -> second yesn-directory
 */
enum iyesde_i_mutex_lock_class
{
	I_MUTEX_NORMAL,
	I_MUTEX_PARENT,
	I_MUTEX_CHILD,
	I_MUTEX_XATTR,
	I_MUTEX_NONDIR2,
	I_MUTEX_PARENT2,
};

static inline void iyesde_lock(struct iyesde *iyesde)
{
	down_write(&iyesde->i_rwsem);
}

static inline void iyesde_unlock(struct iyesde *iyesde)
{
	up_write(&iyesde->i_rwsem);
}

static inline void iyesde_lock_shared(struct iyesde *iyesde)
{
	down_read(&iyesde->i_rwsem);
}

static inline void iyesde_unlock_shared(struct iyesde *iyesde)
{
	up_read(&iyesde->i_rwsem);
}

static inline int iyesde_trylock(struct iyesde *iyesde)
{
	return down_write_trylock(&iyesde->i_rwsem);
}

static inline int iyesde_trylock_shared(struct iyesde *iyesde)
{
	return down_read_trylock(&iyesde->i_rwsem);
}

static inline int iyesde_is_locked(struct iyesde *iyesde)
{
	return rwsem_is_locked(&iyesde->i_rwsem);
}

static inline void iyesde_lock_nested(struct iyesde *iyesde, unsigned subclass)
{
	down_write_nested(&iyesde->i_rwsem, subclass);
}

static inline void iyesde_lock_shared_nested(struct iyesde *iyesde, unsigned subclass)
{
	down_read_nested(&iyesde->i_rwsem, subclass);
}

void lock_two_yesndirectories(struct iyesde *, struct iyesde*);
void unlock_two_yesndirectories(struct iyesde *, struct iyesde*);

/*
 * NOTE: in a 32bit arch with a preemptable kernel and
 * an UP compile the i_size_read/write must be atomic
 * with respect to the local cpu (unlike with preempt disabled),
 * but they don't need to be atomic with respect to other cpus like in
 * true SMP (so they need either to either locally disable irq around
 * the read or for example on x86 they can be still implemented as a
 * cmpxchg8b without the need of the lock prefix). For SMP compiles
 * and 64bit archs it makes yes difference if preempt is enabled or yest.
 */
static inline loff_t i_size_read(const struct iyesde *iyesde)
{
#if BITS_PER_LONG==32 && defined(CONFIG_SMP)
	loff_t i_size;
	unsigned int seq;

	do {
		seq = read_seqcount_begin(&iyesde->i_size_seqcount);
		i_size = iyesde->i_size;
	} while (read_seqcount_retry(&iyesde->i_size_seqcount, seq));
	return i_size;
#elif BITS_PER_LONG==32 && defined(CONFIG_PREEMPT)
	loff_t i_size;

	preempt_disable();
	i_size = iyesde->i_size;
	preempt_enable();
	return i_size;
#else
	return iyesde->i_size;
#endif
}

/*
 * NOTE: unlike i_size_read(), i_size_write() does need locking around it
 * (yesrmally i_mutex), otherwise on 32bit/SMP an update of i_size_seqcount
 * can be lost, resulting in subsequent i_size_read() calls spinning forever.
 */
static inline void i_size_write(struct iyesde *iyesde, loff_t i_size)
{
#if BITS_PER_LONG==32 && defined(CONFIG_SMP)
	preempt_disable();
	write_seqcount_begin(&iyesde->i_size_seqcount);
	iyesde->i_size = i_size;
	write_seqcount_end(&iyesde->i_size_seqcount);
	preempt_enable();
#elif BITS_PER_LONG==32 && defined(CONFIG_PREEMPT)
	preempt_disable();
	iyesde->i_size = i_size;
	preempt_enable();
#else
	iyesde->i_size = i_size;
#endif
}

static inline unsigned imiyesr(const struct iyesde *iyesde)
{
	return MINOR(iyesde->i_rdev);
}

static inline unsigned imajor(const struct iyesde *iyesde)
{
	return MAJOR(iyesde->i_rdev);
}

extern struct block_device *I_BDEV(struct iyesde *iyesde);

struct fown_struct {
	rwlock_t lock;          /* protects pid, uid, euid fields */
	struct pid *pid;	/* pid or -pgrp where SIGIO should be sent */
	enum pid_type pid_type;	/* Kind of process group SIGIO should be sent to */
	kuid_t uid, euid;	/* uid/euid of process setting the owner */
	int signum;		/* posix.1b rt signal to be delivered on IO */
};

/*
 * Track a single file's readahead state
 */
struct file_ra_state {
	pgoff_t start;			/* where readahead started */
	unsigned int size;		/* # of readahead pages */
	unsigned int async_size;	/* do asynchroyesus readahead when
					   there are only # of pages ahead */

	unsigned int ra_pages;		/* Maximum readahead window */
	unsigned int mmap_miss;		/* Cache miss stat for mmap accesses */
	loff_t prev_pos;		/* Cache last read() position */
};

/*
 * Check if @index falls in the readahead windows.
 */
static inline int ra_has_index(struct file_ra_state *ra, pgoff_t index)
{
	return (index >= ra->start &&
		index <  ra->start + ra->size);
}

struct file {
	union {
		struct llist_yesde	fu_llist;
		struct rcu_head 	fu_rcuhead;
	} f_u;
	struct path		f_path;
	struct iyesde		*f_iyesde;	/* cached value */
	const struct file_operations	*f_op;

	/*
	 * Protects f_ep_links, f_flags.
	 * Must yest be taken from IRQ context.
	 */
	spinlock_t		f_lock;
	enum rw_hint		f_write_hint;
	atomic_long_t		f_count;
	unsigned int 		f_flags;
	fmode_t			f_mode;
	struct mutex		f_pos_lock;
	loff_t			f_pos;
	struct fown_struct	f_owner;
	const struct cred	*f_cred;
	struct file_ra_state	f_ra;

	u64			f_version;
#ifdef CONFIG_SECURITY
	void			*f_security;
#endif
	/* needed for tty driver, and maybe others */
	void			*private_data;

#ifdef CONFIG_EPOLL
	/* Used by fs/eventpoll.c to link all the hooks to this file */
	struct list_head	f_ep_links;
	struct list_head	f_tfile_llink;
#endif /* #ifdef CONFIG_EPOLL */
	struct address_space	*f_mapping;
	errseq_t		f_wb_err;
} __randomize_layout
  __attribute__((aligned(4)));	/* lest something weird decides that 2 is OK */

struct file_handle {
	__u32 handle_bytes;
	int handle_type;
	/* file identifier */
	unsigned char f_handle[0];
};

static inline struct file *get_file(struct file *f)
{
	atomic_long_inc(&f->f_count);
	return f;
}
#define get_file_rcu_many(x, cnt)	\
	atomic_long_add_unless(&(x)->f_count, (cnt), 0)
#define get_file_rcu(x) get_file_rcu_many((x), 1)
#define file_count(x)	atomic_long_read(&(x)->f_count)

#define	MAX_NON_LFS	((1UL<<31) - 1)

/* Page cache limit. The filesystems should put that into their s_maxbytes 
   limits, otherwise bad things can happen in VM. */ 
#if BITS_PER_LONG==32
#define MAX_LFS_FILESIZE	((loff_t)ULONG_MAX << PAGE_SHIFT)
#elif BITS_PER_LONG==64
#define MAX_LFS_FILESIZE 	((loff_t)LLONG_MAX)
#endif

#define FL_POSIX	1
#define FL_FLOCK	2
#define FL_DELEG	4	/* NFSv4 delegation */
#define FL_ACCESS	8	/* yest trying to lock, just looking */
#define FL_EXISTS	16	/* when unlocking, test for existence */
#define FL_LEASE	32	/* lease held on this file */
#define FL_CLOSE	64	/* unlock on close */
#define FL_SLEEP	128	/* A blocking lock */
#define FL_DOWNGRADE_PENDING	256 /* Lease is being downgraded */
#define FL_UNLOCK_PENDING	512 /* Lease is being broken */
#define FL_OFDLCK	1024	/* lock is "owned" by struct file */
#define FL_LAYOUT	2048	/* outstanding pNFS layout */

#define FL_CLOSE_POSIX (FL_POSIX | FL_CLOSE)

/*
 * Special return value from posix_lock_file() and vfs_lock_file() for
 * asynchroyesus locking.
 */
#define FILE_LOCK_DEFERRED 1

/* legacy typedef, should eventually be removed */
typedef void *fl_owner_t;

struct file_lock;

struct file_lock_operations {
	void (*fl_copy_lock)(struct file_lock *, struct file_lock *);
	void (*fl_release_private)(struct file_lock *);
};

struct lock_manager_operations {
	fl_owner_t (*lm_get_owner)(fl_owner_t);
	void (*lm_put_owner)(fl_owner_t);
	void (*lm_yestify)(struct file_lock *);	/* unblock callback */
	int (*lm_grant)(struct file_lock *, int);
	bool (*lm_break)(struct file_lock *);
	int (*lm_change)(struct file_lock *, int, struct list_head *);
	void (*lm_setup)(struct file_lock *, void **);
};

struct lock_manager {
	struct list_head list;
	/*
	 * NFSv4 and up also want opens blocked during the grace period;
	 * NLM doesn't care:
	 */
	bool block_opens;
};

struct net;
void locks_start_grace(struct net *, struct lock_manager *);
void locks_end_grace(struct lock_manager *);
bool locks_in_grace(struct net *);
bool opens_in_grace(struct net *);

/* that will die - we need it for nfs_lock_info */
#include <linux/nfs_fs_i.h>

/*
 * struct file_lock represents a generic "file lock". It's used to represent
 * POSIX byte range locks, BSD (flock) locks, and leases. It's important to
 * yeste that the same struct is used to represent both a request for a lock and
 * the lock itself, but the same object is never used for both.
 *
 * FIXME: should we create a separate "struct lock_request" to help distinguish
 * these two uses?
 *
 * The varous i_flctx lists are ordered by:
 *
 * 1) lock owner
 * 2) lock range start
 * 3) lock range end
 *
 * Obviously, the last two criteria only matter for POSIX locks.
 */
struct file_lock {
	struct file_lock *fl_blocker;	/* The lock, that is blocking us */
	struct list_head fl_list;	/* link into file_lock_context */
	struct hlist_yesde fl_link;	/* yesde in global lists */
	struct list_head fl_blocked_requests;	/* list of requests with
						 * ->fl_blocker pointing here
						 */
	struct list_head fl_blocked_member;	/* yesde in
						 * ->fl_blocker->fl_blocked_requests
						 */
	fl_owner_t fl_owner;
	unsigned int fl_flags;
	unsigned char fl_type;
	unsigned int fl_pid;
	int fl_link_cpu;		/* what cpu's list is this on? */
	wait_queue_head_t fl_wait;
	struct file *fl_file;
	loff_t fl_start;
	loff_t fl_end;

	struct fasync_struct *	fl_fasync; /* for lease break yestifications */
	/* for lease breaks: */
	unsigned long fl_break_time;
	unsigned long fl_downgrade_time;

	const struct file_lock_operations *fl_ops;	/* Callbacks for filesystems */
	const struct lock_manager_operations *fl_lmops;	/* Callbacks for lockmanagers */
	union {
		struct nfs_lock_info	nfs_fl;
		struct nfs4_lock_info	nfs4_fl;
		struct {
			struct list_head link;	/* link in AFS vyesde's pending_locks list */
			int state;		/* state of grant or error if -ve */
			unsigned int	debug_id;
		} afs;
	} fl_u;
} __randomize_layout;

struct file_lock_context {
	spinlock_t		flc_lock;
	struct list_head	flc_flock;
	struct list_head	flc_posix;
	struct list_head	flc_lease;
};

/* The following constant reflects the upper bound of the file/locking space */
#ifndef OFFSET_MAX
#define INT_LIMIT(x)	(~((x)1 << (sizeof(x)*8 - 1)))
#define OFFSET_MAX	INT_LIMIT(loff_t)
#define OFFT_OFFSET_MAX	INT_LIMIT(off_t)
#endif

extern void send_sigio(struct fown_struct *fown, int fd, int band);

#define locks_iyesde(f) file_iyesde(f)

#ifdef CONFIG_FILE_LOCKING
extern int fcntl_getlk(struct file *, unsigned int, struct flock *);
extern int fcntl_setlk(unsigned int, struct file *, unsigned int,
			struct flock *);

#if BITS_PER_LONG == 32
extern int fcntl_getlk64(struct file *, unsigned int, struct flock64 *);
extern int fcntl_setlk64(unsigned int, struct file *, unsigned int,
			struct flock64 *);
#endif

extern int fcntl_setlease(unsigned int fd, struct file *filp, long arg);
extern int fcntl_getlease(struct file *filp);

/* fs/locks.c */
void locks_free_lock_context(struct iyesde *iyesde);
void locks_free_lock(struct file_lock *fl);
extern void locks_init_lock(struct file_lock *);
extern struct file_lock * locks_alloc_lock(void);
extern void locks_copy_lock(struct file_lock *, struct file_lock *);
extern void locks_copy_conflock(struct file_lock *, struct file_lock *);
extern void locks_remove_posix(struct file *, fl_owner_t);
extern void locks_remove_file(struct file *);
extern void locks_release_private(struct file_lock *);
extern void posix_test_lock(struct file *, struct file_lock *);
extern int posix_lock_file(struct file *, struct file_lock *, struct file_lock *);
extern int locks_delete_block(struct file_lock *);
extern int vfs_test_lock(struct file *, struct file_lock *);
extern int vfs_lock_file(struct file *, unsigned int, struct file_lock *, struct file_lock *);
extern int vfs_cancel_lock(struct file *filp, struct file_lock *fl);
extern int locks_lock_iyesde_wait(struct iyesde *iyesde, struct file_lock *fl);
extern int __break_lease(struct iyesde *iyesde, unsigned int flags, unsigned int type);
extern void lease_get_mtime(struct iyesde *, struct timespec64 *time);
extern int generic_setlease(struct file *, long, struct file_lock **, void **priv);
extern int vfs_setlease(struct file *, long, struct file_lock **, void **);
extern int lease_modify(struct file_lock *, int, struct list_head *);

struct yestifier_block;
extern int lease_register_yestifier(struct yestifier_block *);
extern void lease_unregister_yestifier(struct yestifier_block *);

struct files_struct;
extern void show_fd_locks(struct seq_file *f,
			 struct file *filp, struct files_struct *files);
#else /* !CONFIG_FILE_LOCKING */
static inline int fcntl_getlk(struct file *file, unsigned int cmd,
			      struct flock __user *user)
{
	return -EINVAL;
}

static inline int fcntl_setlk(unsigned int fd, struct file *file,
			      unsigned int cmd, struct flock __user *user)
{
	return -EACCES;
}

#if BITS_PER_LONG == 32
static inline int fcntl_getlk64(struct file *file, unsigned int cmd,
				struct flock64 __user *user)
{
	return -EINVAL;
}

static inline int fcntl_setlk64(unsigned int fd, struct file *file,
				unsigned int cmd, struct flock64 __user *user)
{
	return -EACCES;
}
#endif
static inline int fcntl_setlease(unsigned int fd, struct file *filp, long arg)
{
	return -EINVAL;
}

static inline int fcntl_getlease(struct file *filp)
{
	return F_UNLCK;
}

static inline void
locks_free_lock_context(struct iyesde *iyesde)
{
}

static inline void locks_init_lock(struct file_lock *fl)
{
	return;
}

static inline void locks_copy_conflock(struct file_lock *new, struct file_lock *fl)
{
	return;
}

static inline void locks_copy_lock(struct file_lock *new, struct file_lock *fl)
{
	return;
}

static inline void locks_remove_posix(struct file *filp, fl_owner_t owner)
{
	return;
}

static inline void locks_remove_file(struct file *filp)
{
	return;
}

static inline void posix_test_lock(struct file *filp, struct file_lock *fl)
{
	return;
}

static inline int posix_lock_file(struct file *filp, struct file_lock *fl,
				  struct file_lock *conflock)
{
	return -ENOLCK;
}

static inline int locks_delete_block(struct file_lock *waiter)
{
	return -ENOENT;
}

static inline int vfs_test_lock(struct file *filp, struct file_lock *fl)
{
	return 0;
}

static inline int vfs_lock_file(struct file *filp, unsigned int cmd,
				struct file_lock *fl, struct file_lock *conf)
{
	return -ENOLCK;
}

static inline int vfs_cancel_lock(struct file *filp, struct file_lock *fl)
{
	return 0;
}

static inline int locks_lock_iyesde_wait(struct iyesde *iyesde, struct file_lock *fl)
{
	return -ENOLCK;
}

static inline int __break_lease(struct iyesde *iyesde, unsigned int mode, unsigned int type)
{
	return 0;
}

static inline void lease_get_mtime(struct iyesde *iyesde,
				   struct timespec64 *time)
{
	return;
}

static inline int generic_setlease(struct file *filp, long arg,
				    struct file_lock **flp, void **priv)
{
	return -EINVAL;
}

static inline int vfs_setlease(struct file *filp, long arg,
			       struct file_lock **lease, void **priv)
{
	return -EINVAL;
}

static inline int lease_modify(struct file_lock *fl, int arg,
			       struct list_head *dispose)
{
	return -EINVAL;
}

struct files_struct;
static inline void show_fd_locks(struct seq_file *f,
			struct file *filp, struct files_struct *files) {}
#endif /* !CONFIG_FILE_LOCKING */

static inline struct iyesde *file_iyesde(const struct file *f)
{
	return f->f_iyesde;
}

static inline struct dentry *file_dentry(const struct file *file)
{
	return d_real(file->f_path.dentry, file_iyesde(file));
}

static inline int locks_lock_file_wait(struct file *filp, struct file_lock *fl)
{
	return locks_lock_iyesde_wait(locks_iyesde(filp), fl);
}

struct fasync_struct {
	rwlock_t		fa_lock;
	int			magic;
	int			fa_fd;
	struct fasync_struct	*fa_next; /* singly linked list */
	struct file		*fa_file;
	struct rcu_head		fa_rcu;
};

#define FASYNC_MAGIC 0x4601

/* SMP safe fasync helpers: */
extern int fasync_helper(int, struct file *, int, struct fasync_struct **);
extern struct fasync_struct *fasync_insert_entry(int, struct file *, struct fasync_struct **, struct fasync_struct *);
extern int fasync_remove_entry(struct file *, struct fasync_struct **);
extern struct fasync_struct *fasync_alloc(void);
extern void fasync_free(struct fasync_struct *);

/* can be called from interrupts */
extern void kill_fasync(struct fasync_struct **, int, int);

extern void __f_setown(struct file *filp, struct pid *, enum pid_type, int force);
extern int f_setown(struct file *filp, unsigned long arg, int force);
extern void f_delown(struct file *filp);
extern pid_t f_getown(struct file *filp);
extern int send_sigurg(struct fown_struct *fown);

/*
 * sb->s_flags.  Note that these mirror the equivalent MS_* flags where
 * represented in both.
 */
#define SB_RDONLY	 1	/* Mount read-only */
#define SB_NOSUID	 2	/* Igyesre suid and sgid bits */
#define SB_NODEV	 4	/* Disallow access to device special files */
#define SB_NOEXEC	 8	/* Disallow program execution */
#define SB_SYNCHRONOUS	16	/* Writes are synced at once */
#define SB_MANDLOCK	64	/* Allow mandatory locks on an FS */
#define SB_DIRSYNC	128	/* Directory modifications are synchroyesus */
#define SB_NOATIME	1024	/* Do yest update access times. */
#define SB_NODIRATIME	2048	/* Do yest update directory access times */
#define SB_SILENT	32768
#define SB_POSIXACL	(1<<16)	/* VFS does yest apply the umask */
#define SB_KERNMOUNT	(1<<22) /* this is a kern_mount call */
#define SB_I_VERSION	(1<<23) /* Update iyesde I_version field */
#define SB_LAZYTIME	(1<<25) /* Update the on-disk [acm]times lazily */

/* These sb flags are internal to the kernel */
#define SB_SUBMOUNT     (1<<26)
#define SB_FORCE    	(1<<27)
#define SB_NOSEC	(1<<28)
#define SB_BORN		(1<<29)
#define SB_ACTIVE	(1<<30)
#define SB_NOUSER	(1<<31)

/*
 *	Umount options
 */

#define MNT_FORCE	0x00000001	/* Attempt to forcibily umount */
#define MNT_DETACH	0x00000002	/* Just detach from the tree */
#define MNT_EXPIRE	0x00000004	/* Mark for expiry */
#define UMOUNT_NOFOLLOW	0x00000008	/* Don't follow symlink on umount */
#define UMOUNT_UNUSED	0x80000000	/* Flag guaranteed to be unused */

/* sb->s_iflags */
#define SB_I_CGROUPWB	0x00000001	/* cgroup-aware writeback enabled */
#define SB_I_NOEXEC	0x00000002	/* Igyesre executables on this fs */
#define SB_I_NODEV	0x00000004	/* Igyesre devices on this fs */
#define SB_I_MULTIROOT	0x00000008	/* Multiple roots to the dentry tree */

/* sb->s_iflags to limit user namespace mounts */
#define SB_I_USERNS_VISIBLE		0x00000010 /* fstype already mounted */
#define SB_I_IMA_UNVERIFIABLE_SIGNATURE	0x00000020
#define SB_I_UNTRUSTED_MOUNTER		0x00000040

/* Possible states of 'frozen' field */
enum {
	SB_UNFROZEN = 0,		/* FS is unfrozen */
	SB_FREEZE_WRITE	= 1,		/* Writes, dir ops, ioctls frozen */
	SB_FREEZE_PAGEFAULT = 2,	/* Page faults stopped as well */
	SB_FREEZE_FS = 3,		/* For internal FS use (e.g. to stop
					 * internal threads if needed) */
	SB_FREEZE_COMPLETE = 4,		/* ->freeze_fs finished successfully */
};

#define SB_FREEZE_LEVELS (SB_FREEZE_COMPLETE - 1)

struct sb_writers {
	int				frozen;		/* Is sb frozen? */
	wait_queue_head_t		wait_unfrozen;	/* for get_super_thawed() */
	struct percpu_rw_semaphore	rw_sem[SB_FREEZE_LEVELS];
};

struct super_block {
	struct list_head	s_list;		/* Keep this first */
	dev_t			s_dev;		/* search index; _yest_ kdev_t */
	unsigned char		s_blocksize_bits;
	unsigned long		s_blocksize;
	loff_t			s_maxbytes;	/* Max file size */
	struct file_system_type	*s_type;
	const struct super_operations	*s_op;
	const struct dquot_operations	*dq_op;
	const struct quotactl_ops	*s_qcop;
	const struct export_operations *s_export_op;
	unsigned long		s_flags;
	unsigned long		s_iflags;	/* internal SB_I_* flags */
	unsigned long		s_magic;
	struct dentry		*s_root;
	struct rw_semaphore	s_umount;
	int			s_count;
	atomic_t		s_active;
#ifdef CONFIG_SECURITY
	void                    *s_security;
#endif
	const struct xattr_handler **s_xattr;
#ifdef CONFIG_FS_ENCRYPTION
	const struct fscrypt_operations	*s_cop;
	struct key		*s_master_keys; /* master crypto keys in use */
#endif
#ifdef CONFIG_FS_VERITY
	const struct fsverity_operations *s_vop;
#endif
	struct hlist_bl_head	s_roots;	/* alternate root dentries for NFS */
	struct list_head	s_mounts;	/* list of mounts; _yest_ for fs use */
	struct block_device	*s_bdev;
	struct backing_dev_info *s_bdi;
	struct mtd_info		*s_mtd;
	struct hlist_yesde	s_instances;
	unsigned int		s_quota_types;	/* Bitmask of supported quota types */
	struct quota_info	s_dquot;	/* Diskquota specific options */

	struct sb_writers	s_writers;

	/*
	 * Keep s_fs_info, s_time_gran, s_fsyestify_mask, and
	 * s_fsyestify_marks together for cache efficiency. They are frequently
	 * accessed and rarely modified.
	 */
	void			*s_fs_info;	/* Filesystem private info */

	/* Granularity of c/m/atime in ns (canyest be worse than a second) */
	u32			s_time_gran;
	/* Time limits for c/m/atime in seconds */
	time64_t		   s_time_min;
	time64_t		   s_time_max;
#ifdef CONFIG_FSNOTIFY
	__u32			s_fsyestify_mask;
	struct fsyestify_mark_connector __rcu	*s_fsyestify_marks;
#endif

	char			s_id[32];	/* Informational name */
	uuid_t			s_uuid;		/* UUID */

	unsigned int		s_max_links;
	fmode_t			s_mode;

	/*
	 * The next field is for VFS *only*. No filesystems have any business
	 * even looking at it. You had been warned.
	 */
	struct mutex s_vfs_rename_mutex;	/* Kludge */

	/*
	 * Filesystem subtype.  If yesn-empty the filesystem type field
	 * in /proc/mounts will be "type.subtype"
	 */
	const char *s_subtype;

	const struct dentry_operations *s_d_op; /* default d_op for dentries */

	/*
	 * Saved pool identifier for cleancache (-1 means yesne)
	 */
	int cleancache_poolid;

	struct shrinker s_shrink;	/* per-sb shrinker handle */

	/* Number of iyesdes with nlink == 0 but still referenced */
	atomic_long_t s_remove_count;

	/* Pending fsyestify iyesde refs */
	atomic_long_t s_fsyestify_iyesde_refs;

	/* Being remounted read-only */
	int s_readonly_remount;

	/* AIO completions deferred from interrupt context */
	struct workqueue_struct *s_dio_done_wq;
	struct hlist_head s_pins;

	/*
	 * Owning user namespace and default context in which to
	 * interpret filesystem uids, gids, quotas, device yesdes,
	 * xattrs and security labels.
	 */
	struct user_namespace *s_user_ns;

	/*
	 * The list_lru structure is essentially just a pointer to a table
	 * of per-yesde lru lists, each of which has its own spinlock.
	 * There is yes need to put them into separate cachelines.
	 */
	struct list_lru		s_dentry_lru;
	struct list_lru		s_iyesde_lru;
	struct rcu_head		rcu;
	struct work_struct	destroy_work;

	struct mutex		s_sync_lock;	/* sync serialisation lock */

	/*
	 * Indicates how deep in a filesystem stack this SB is
	 */
	int s_stack_depth;

	/* s_iyesde_list_lock protects s_iyesdes */
	spinlock_t		s_iyesde_list_lock ____cacheline_aligned_in_smp;
	struct list_head	s_iyesdes;	/* all iyesdes */

	spinlock_t		s_iyesde_wblist_lock;
	struct list_head	s_iyesdes_wb;	/* writeback iyesdes */
} __randomize_layout;

/* Helper functions so that in most cases filesystems will
 * yest need to deal directly with kuid_t and kgid_t and can
 * instead deal with the raw numeric values that are stored
 * in the filesystem.
 */
static inline uid_t i_uid_read(const struct iyesde *iyesde)
{
	return from_kuid(iyesde->i_sb->s_user_ns, iyesde->i_uid);
}

static inline gid_t i_gid_read(const struct iyesde *iyesde)
{
	return from_kgid(iyesde->i_sb->s_user_ns, iyesde->i_gid);
}

static inline void i_uid_write(struct iyesde *iyesde, uid_t uid)
{
	iyesde->i_uid = make_kuid(iyesde->i_sb->s_user_ns, uid);
}

static inline void i_gid_write(struct iyesde *iyesde, gid_t gid)
{
	iyesde->i_gid = make_kgid(iyesde->i_sb->s_user_ns, gid);
}

extern struct timespec64 timespec64_trunc(struct timespec64 t, unsigned gran);
extern struct timespec64 current_time(struct iyesde *iyesde);

/*
 * Snapshotting support.
 */

void __sb_end_write(struct super_block *sb, int level);
int __sb_start_write(struct super_block *sb, int level, bool wait);

#define __sb_writers_acquired(sb, lev)	\
	percpu_rwsem_acquire(&(sb)->s_writers.rw_sem[(lev)-1], 1, _THIS_IP_)
#define __sb_writers_release(sb, lev)	\
	percpu_rwsem_release(&(sb)->s_writers.rw_sem[(lev)-1], 1, _THIS_IP_)

/**
 * sb_end_write - drop write access to a superblock
 * @sb: the super we wrote to
 *
 * Decrement number of writers to the filesystem. Wake up possible waiters
 * wanting to freeze the filesystem.
 */
static inline void sb_end_write(struct super_block *sb)
{
	__sb_end_write(sb, SB_FREEZE_WRITE);
}

/**
 * sb_end_pagefault - drop write access to a superblock from a page fault
 * @sb: the super we wrote to
 *
 * Decrement number of processes handling write page fault to the filesystem.
 * Wake up possible waiters wanting to freeze the filesystem.
 */
static inline void sb_end_pagefault(struct super_block *sb)
{
	__sb_end_write(sb, SB_FREEZE_PAGEFAULT);
}

/**
 * sb_end_intwrite - drop write access to a superblock for internal fs purposes
 * @sb: the super we wrote to
 *
 * Decrement fs-internal number of writers to the filesystem.  Wake up possible
 * waiters wanting to freeze the filesystem.
 */
static inline void sb_end_intwrite(struct super_block *sb)
{
	__sb_end_write(sb, SB_FREEZE_FS);
}

/**
 * sb_start_write - get write access to a superblock
 * @sb: the super we write to
 *
 * When a process wants to write data or metadata to a file system (i.e. dirty
 * a page or an iyesde), it should embed the operation in a sb_start_write() -
 * sb_end_write() pair to get exclusion against file system freezing. This
 * function increments number of writers preventing freezing. If the file
 * system is already frozen, the function waits until the file system is
 * thawed.
 *
 * Since freeze protection behaves as a lock, users have to preserve
 * ordering of freeze protection and other filesystem locks. Generally,
 * freeze protection should be the outermost lock. In particular, we have:
 *
 * sb_start_write
 *   -> i_mutex			(write path, truncate, directory ops, ...)
 *   -> s_umount		(freeze_super, thaw_super)
 */
static inline void sb_start_write(struct super_block *sb)
{
	__sb_start_write(sb, SB_FREEZE_WRITE, true);
}

static inline int sb_start_write_trylock(struct super_block *sb)
{
	return __sb_start_write(sb, SB_FREEZE_WRITE, false);
}

/**
 * sb_start_pagefault - get write access to a superblock from a page fault
 * @sb: the super we write to
 *
 * When a process starts handling write page fault, it should embed the
 * operation into sb_start_pagefault() - sb_end_pagefault() pair to get
 * exclusion against file system freezing. This is needed since the page fault
 * is going to dirty a page. This function increments number of running page
 * faults preventing freezing. If the file system is already frozen, the
 * function waits until the file system is thawed.
 *
 * Since page fault freeze protection behaves as a lock, users have to preserve
 * ordering of freeze protection and other filesystem locks. It is advised to
 * put sb_start_pagefault() close to mmap_sem in lock ordering. Page fault
 * handling code implies lock dependency:
 *
 * mmap_sem
 *   -> sb_start_pagefault
 */
static inline void sb_start_pagefault(struct super_block *sb)
{
	__sb_start_write(sb, SB_FREEZE_PAGEFAULT, true);
}

/*
 * sb_start_intwrite - get write access to a superblock for internal fs purposes
 * @sb: the super we write to
 *
 * This is the third level of protection against filesystem freezing. It is
 * free for use by a filesystem. The only requirement is that it must rank
 * below sb_start_pagefault.
 *
 * For example filesystem can call sb_start_intwrite() when starting a
 * transaction which somewhat eases handling of freezing for internal sources
 * of filesystem changes (internal fs threads, discarding preallocation on file
 * close, etc.).
 */
static inline void sb_start_intwrite(struct super_block *sb)
{
	__sb_start_write(sb, SB_FREEZE_FS, true);
}

static inline int sb_start_intwrite_trylock(struct super_block *sb)
{
	return __sb_start_write(sb, SB_FREEZE_FS, false);
}


extern bool iyesde_owner_or_capable(const struct iyesde *iyesde);

/*
 * VFS helper functions..
 */
extern int vfs_create(struct iyesde *, struct dentry *, umode_t, bool);
extern int vfs_mkdir(struct iyesde *, struct dentry *, umode_t);
extern int vfs_mkyesd(struct iyesde *, struct dentry *, umode_t, dev_t);
extern int vfs_symlink(struct iyesde *, struct dentry *, const char *);
extern int vfs_link(struct dentry *, struct iyesde *, struct dentry *, struct iyesde **);
extern int vfs_rmdir(struct iyesde *, struct dentry *);
extern int vfs_unlink(struct iyesde *, struct dentry *, struct iyesde **);
extern int vfs_rename(struct iyesde *, struct dentry *, struct iyesde *, struct dentry *, struct iyesde **, unsigned int);
extern int vfs_whiteout(struct iyesde *, struct dentry *);

extern struct dentry *vfs_tmpfile(struct dentry *dentry, umode_t mode,
				  int open_flag);

int vfs_mkobj(struct dentry *, umode_t,
		int (*f)(struct dentry *, umode_t, void *),
		void *);

extern long vfs_ioctl(struct file *file, unsigned int cmd, unsigned long arg);

#ifdef CONFIG_COMPAT
extern long compat_ptr_ioctl(struct file *file, unsigned int cmd,
					unsigned long arg);
#else
#define compat_ptr_ioctl NULL
#endif

/*
 * VFS file helper functions.
 */
extern void iyesde_init_owner(struct iyesde *iyesde, const struct iyesde *dir,
			umode_t mode);
extern bool may_open_dev(const struct path *path);
/*
 * VFS FS_IOC_FIEMAP helper definitions.
 */
struct fiemap_extent_info {
	unsigned int fi_flags;		/* Flags as passed from user */
	unsigned int fi_extents_mapped;	/* Number of mapped extents */
	unsigned int fi_extents_max;	/* Size of fiemap_extent array */
	struct fiemap_extent __user *fi_extents_start; /* Start of
							fiemap_extent array */
};
int fiemap_fill_next_extent(struct fiemap_extent_info *info, u64 logical,
			    u64 phys, u64 len, u32 flags);
int fiemap_check_flags(struct fiemap_extent_info *fieinfo, u32 fs_flags);

/*
 * This is the "filldir" function type, used by readdir() to let
 * the kernel specify what kind of dirent layout it wants to have.
 * This allows the kernel to read directories into kernel space or
 * to have different dirent layouts depending on the binary type.
 */
struct dir_context;
typedef int (*filldir_t)(struct dir_context *, const char *, int, loff_t, u64,
			 unsigned);

struct dir_context {
	filldir_t actor;
	loff_t pos;
};

struct block_device_operations;

/* These macros are for out of kernel modules to test that
 * the kernel supports the unlocked_ioctl and compat_ioctl
 * fields in struct file_operations. */
#define HAVE_COMPAT_IOCTL 1
#define HAVE_UNLOCKED_IOCTL 1

/*
 * These flags let !MMU mmap() govern direct device mapping vs immediate
 * copying more easily for MAP_PRIVATE, especially for ROM filesystems.
 *
 * NOMMU_MAP_COPY:	Copy can be mapped (MAP_PRIVATE)
 * NOMMU_MAP_DIRECT:	Can be mapped directly (MAP_SHARED)
 * NOMMU_MAP_READ:	Can be mapped for reading
 * NOMMU_MAP_WRITE:	Can be mapped for writing
 * NOMMU_MAP_EXEC:	Can be mapped for execution
 */
#define NOMMU_MAP_COPY		0x00000001
#define NOMMU_MAP_DIRECT	0x00000008
#define NOMMU_MAP_READ		VM_MAYREAD
#define NOMMU_MAP_WRITE		VM_MAYWRITE
#define NOMMU_MAP_EXEC		VM_MAYEXEC

#define NOMMU_VMFLAGS \
	(NOMMU_MAP_READ | NOMMU_MAP_WRITE | NOMMU_MAP_EXEC)

/*
 * These flags control the behavior of the remap_file_range function pointer.
 * If it is called with len == 0 that means "remap to end of source file".
 * See Documentation/filesystems/vfs.rst for more details about this call.
 *
 * REMAP_FILE_DEDUP: only remap if contents identical (i.e. deduplicate)
 * REMAP_FILE_CAN_SHORTEN: caller can handle a shortened request
 */
#define REMAP_FILE_DEDUP		(1 << 0)
#define REMAP_FILE_CAN_SHORTEN		(1 << 1)

/*
 * These flags signal that the caller is ok with altering various aspects of
 * the behavior of the remap operation.  The changes must be made by the
 * implementation; the vfs remap helper functions can take advantage of them.
 * Flags in this category exist to preserve the quirky behavior of the hoisted
 * btrfs clone/dedupe ioctls.
 */
#define REMAP_FILE_ADVISORY		(REMAP_FILE_CAN_SHORTEN)

struct iov_iter;

struct file_operations {
	struct module *owner;
	loff_t (*llseek) (struct file *, loff_t, int);
	ssize_t (*read) (struct file *, char __user *, size_t, loff_t *);
	ssize_t (*write) (struct file *, const char __user *, size_t, loff_t *);
	ssize_t (*read_iter) (struct kiocb *, struct iov_iter *);
	ssize_t (*write_iter) (struct kiocb *, struct iov_iter *);
	int (*iopoll)(struct kiocb *kiocb, bool spin);
	int (*iterate) (struct file *, struct dir_context *);
	int (*iterate_shared) (struct file *, struct dir_context *);
	__poll_t (*poll) (struct file *, struct poll_table_struct *);
	long (*unlocked_ioctl) (struct file *, unsigned int, unsigned long);
	long (*compat_ioctl) (struct file *, unsigned int, unsigned long);
	int (*mmap) (struct file *, struct vm_area_struct *);
	unsigned long mmap_supported_flags;
	int (*open) (struct iyesde *, struct file *);
	int (*flush) (struct file *, fl_owner_t id);
	int (*release) (struct iyesde *, struct file *);
	int (*fsync) (struct file *, loff_t, loff_t, int datasync);
	int (*fasync) (int, struct file *, int);
	int (*lock) (struct file *, int, struct file_lock *);
	ssize_t (*sendpage) (struct file *, struct page *, int, size_t, loff_t *, int);
	unsigned long (*get_unmapped_area)(struct file *, unsigned long, unsigned long, unsigned long, unsigned long);
	int (*check_flags)(int);
	int (*flock) (struct file *, int, struct file_lock *);
	ssize_t (*splice_write)(struct pipe_iyesde_info *, struct file *, loff_t *, size_t, unsigned int);
	ssize_t (*splice_read)(struct file *, loff_t *, struct pipe_iyesde_info *, size_t, unsigned int);
	int (*setlease)(struct file *, long, struct file_lock **, void **);
	long (*fallocate)(struct file *file, int mode, loff_t offset,
			  loff_t len);
	void (*show_fdinfo)(struct seq_file *m, struct file *f);
#ifndef CONFIG_MMU
	unsigned (*mmap_capabilities)(struct file *);
#endif
	ssize_t (*copy_file_range)(struct file *, loff_t, struct file *,
			loff_t, size_t, unsigned int);
	loff_t (*remap_file_range)(struct file *file_in, loff_t pos_in,
				   struct file *file_out, loff_t pos_out,
				   loff_t len, unsigned int remap_flags);
	int (*fadvise)(struct file *, loff_t, loff_t, int);
} __randomize_layout;

struct iyesde_operations {
	struct dentry * (*lookup) (struct iyesde *,struct dentry *, unsigned int);
	const char * (*get_link) (struct dentry *, struct iyesde *, struct delayed_call *);
	int (*permission) (struct iyesde *, int);
	struct posix_acl * (*get_acl)(struct iyesde *, int);

	int (*readlink) (struct dentry *, char __user *,int);

	int (*create) (struct iyesde *,struct dentry *, umode_t, bool);
	int (*link) (struct dentry *,struct iyesde *,struct dentry *);
	int (*unlink) (struct iyesde *,struct dentry *);
	int (*symlink) (struct iyesde *,struct dentry *,const char *);
	int (*mkdir) (struct iyesde *,struct dentry *,umode_t);
	int (*rmdir) (struct iyesde *,struct dentry *);
	int (*mkyesd) (struct iyesde *,struct dentry *,umode_t,dev_t);
	int (*rename) (struct iyesde *, struct dentry *,
			struct iyesde *, struct dentry *, unsigned int);
	int (*setattr) (struct dentry *, struct iattr *);
	int (*getattr) (const struct path *, struct kstat *, u32, unsigned int);
	ssize_t (*listxattr) (struct dentry *, char *, size_t);
	int (*fiemap)(struct iyesde *, struct fiemap_extent_info *, u64 start,
		      u64 len);
	int (*update_time)(struct iyesde *, struct timespec64 *, int);
	int (*atomic_open)(struct iyesde *, struct dentry *,
			   struct file *, unsigned open_flag,
			   umode_t create_mode);
	int (*tmpfile) (struct iyesde *, struct dentry *, umode_t);
	int (*set_acl)(struct iyesde *, struct posix_acl *, int);
} ____cacheline_aligned;

static inline ssize_t call_read_iter(struct file *file, struct kiocb *kio,
				     struct iov_iter *iter)
{
	return file->f_op->read_iter(kio, iter);
}

static inline ssize_t call_write_iter(struct file *file, struct kiocb *kio,
				      struct iov_iter *iter)
{
	return file->f_op->write_iter(kio, iter);
}

static inline int call_mmap(struct file *file, struct vm_area_struct *vma)
{
	return file->f_op->mmap(file, vma);
}

ssize_t rw_copy_check_uvector(int type, const struct iovec __user * uvector,
			      unsigned long nr_segs, unsigned long fast_segs,
			      struct iovec *fast_pointer,
			      struct iovec **ret_pointer);

extern ssize_t __vfs_read(struct file *, char __user *, size_t, loff_t *);
extern ssize_t vfs_read(struct file *, char __user *, size_t, loff_t *);
extern ssize_t vfs_write(struct file *, const char __user *, size_t, loff_t *);
extern ssize_t vfs_readv(struct file *, const struct iovec __user *,
		unsigned long, loff_t *, rwf_t);
extern ssize_t vfs_copy_file_range(struct file *, loff_t , struct file *,
				   loff_t, size_t, unsigned int);
extern ssize_t generic_copy_file_range(struct file *file_in, loff_t pos_in,
				       struct file *file_out, loff_t pos_out,
				       size_t len, unsigned int flags);
extern int generic_remap_file_range_prep(struct file *file_in, loff_t pos_in,
					 struct file *file_out, loff_t pos_out,
					 loff_t *count,
					 unsigned int remap_flags);
extern loff_t do_clone_file_range(struct file *file_in, loff_t pos_in,
				  struct file *file_out, loff_t pos_out,
				  loff_t len, unsigned int remap_flags);
extern loff_t vfs_clone_file_range(struct file *file_in, loff_t pos_in,
				   struct file *file_out, loff_t pos_out,
				   loff_t len, unsigned int remap_flags);
extern int vfs_dedupe_file_range(struct file *file,
				 struct file_dedupe_range *same);
extern loff_t vfs_dedupe_file_range_one(struct file *src_file, loff_t src_pos,
					struct file *dst_file, loff_t dst_pos,
					loff_t len, unsigned int remap_flags);


struct super_operations {
   	struct iyesde *(*alloc_iyesde)(struct super_block *sb);
	void (*destroy_iyesde)(struct iyesde *);
	void (*free_iyesde)(struct iyesde *);

   	void (*dirty_iyesde) (struct iyesde *, int flags);
	int (*write_iyesde) (struct iyesde *, struct writeback_control *wbc);
	int (*drop_iyesde) (struct iyesde *);
	void (*evict_iyesde) (struct iyesde *);
	void (*put_super) (struct super_block *);
	int (*sync_fs)(struct super_block *sb, int wait);
	int (*freeze_super) (struct super_block *);
	int (*freeze_fs) (struct super_block *);
	int (*thaw_super) (struct super_block *);
	int (*unfreeze_fs) (struct super_block *);
	int (*statfs) (struct dentry *, struct kstatfs *);
	int (*remount_fs) (struct super_block *, int *, char *);
	void (*umount_begin) (struct super_block *);

	int (*show_options)(struct seq_file *, struct dentry *);
	int (*show_devname)(struct seq_file *, struct dentry *);
	int (*show_path)(struct seq_file *, struct dentry *);
	int (*show_stats)(struct seq_file *, struct dentry *);
#ifdef CONFIG_QUOTA
	ssize_t (*quota_read)(struct super_block *, int, char *, size_t, loff_t);
	ssize_t (*quota_write)(struct super_block *, int, const char *, size_t, loff_t);
	struct dquot **(*get_dquots)(struct iyesde *);
#endif
	int (*bdev_try_to_free_page)(struct super_block*, struct page*, gfp_t);
	long (*nr_cached_objects)(struct super_block *,
				  struct shrink_control *);
	long (*free_cached_objects)(struct super_block *,
				    struct shrink_control *);
};

/*
 * Iyesde flags - they have yes relation to superblock flags yesw
 */
#define S_SYNC		1	/* Writes are synced at once */
#define S_NOATIME	2	/* Do yest update access times */
#define S_APPEND	4	/* Append-only file */
#define S_IMMUTABLE	8	/* Immutable file */
#define S_DEAD		16	/* removed, but still open directory */
#define S_NOQUOTA	32	/* Iyesde is yest counted to quota */
#define S_DIRSYNC	64	/* Directory modifications are synchroyesus */
#define S_NOCMTIME	128	/* Do yest update file c/mtime */
#define S_SWAPFILE	256	/* Do yest truncate: swapon got its bmaps */
#define S_PRIVATE	512	/* Iyesde is fs-internal */
#define S_IMA		1024	/* Iyesde has an associated IMA struct */
#define S_AUTOMOUNT	2048	/* Automount/referral quasi-directory */
#define S_NOSEC		4096	/* yes suid or xattr security attributes */
#ifdef CONFIG_FS_DAX
#define S_DAX		8192	/* Direct Access, avoiding the page cache */
#else
#define S_DAX		0	/* Make all the DAX code disappear */
#endif
#define S_ENCRYPTED	16384	/* Encrypted file (using fs/crypto/) */
#define S_CASEFOLD	32768	/* Casefolded file */
#define S_VERITY	65536	/* Verity file (using fs/verity/) */

/*
 * Note that yessuid etc flags are iyesde-specific: setting some file-system
 * flags just means all the iyesdes inherit those flags by default. It might be
 * possible to override it selectively if you really wanted to with some
 * ioctl() that is yest currently implemented.
 *
 * Exception: SB_RDONLY is always applied to the entire file system.
 *
 * Unfortunately, it is possible to change a filesystems flags with it mounted
 * with files in use.  This means that all of the iyesdes will yest have their
 * i_flags updated.  Hence, i_flags yes longer inherit the superblock mount
 * flags, so these have to be checked separately. -- rmk@arm.uk.linux.org
 */
#define __IS_FLG(iyesde, flg)	((iyesde)->i_sb->s_flags & (flg))

static inline bool sb_rdonly(const struct super_block *sb) { return sb->s_flags & SB_RDONLY; }
#define IS_RDONLY(iyesde)	sb_rdonly((iyesde)->i_sb)
#define IS_SYNC(iyesde)		(__IS_FLG(iyesde, SB_SYNCHRONOUS) || \
					((iyesde)->i_flags & S_SYNC))
#define IS_DIRSYNC(iyesde)	(__IS_FLG(iyesde, SB_SYNCHRONOUS|SB_DIRSYNC) || \
					((iyesde)->i_flags & (S_SYNC|S_DIRSYNC)))
#define IS_MANDLOCK(iyesde)	__IS_FLG(iyesde, SB_MANDLOCK)
#define IS_NOATIME(iyesde)	__IS_FLG(iyesde, SB_RDONLY|SB_NOATIME)
#define IS_I_VERSION(iyesde)	__IS_FLG(iyesde, SB_I_VERSION)

#define IS_NOQUOTA(iyesde)	((iyesde)->i_flags & S_NOQUOTA)
#define IS_APPEND(iyesde)	((iyesde)->i_flags & S_APPEND)
#define IS_IMMUTABLE(iyesde)	((iyesde)->i_flags & S_IMMUTABLE)
#define IS_POSIXACL(iyesde)	__IS_FLG(iyesde, SB_POSIXACL)

#define IS_DEADDIR(iyesde)	((iyesde)->i_flags & S_DEAD)
#define IS_NOCMTIME(iyesde)	((iyesde)->i_flags & S_NOCMTIME)
#define IS_SWAPFILE(iyesde)	((iyesde)->i_flags & S_SWAPFILE)
#define IS_PRIVATE(iyesde)	((iyesde)->i_flags & S_PRIVATE)
#define IS_IMA(iyesde)		((iyesde)->i_flags & S_IMA)
#define IS_AUTOMOUNT(iyesde)	((iyesde)->i_flags & S_AUTOMOUNT)
#define IS_NOSEC(iyesde)		((iyesde)->i_flags & S_NOSEC)
#define IS_DAX(iyesde)		((iyesde)->i_flags & S_DAX)
#define IS_ENCRYPTED(iyesde)	((iyesde)->i_flags & S_ENCRYPTED)
#define IS_CASEFOLDED(iyesde)	((iyesde)->i_flags & S_CASEFOLD)
#define IS_VERITY(iyesde)	((iyesde)->i_flags & S_VERITY)

#define IS_WHITEOUT(iyesde)	(S_ISCHR(iyesde->i_mode) && \
				 (iyesde)->i_rdev == WHITEOUT_DEV)

static inline bool HAS_UNMAPPED_ID(struct iyesde *iyesde)
{
	return !uid_valid(iyesde->i_uid) || !gid_valid(iyesde->i_gid);
}

static inline enum rw_hint file_write_hint(struct file *file)
{
	if (file->f_write_hint != WRITE_LIFE_NOT_SET)
		return file->f_write_hint;

	return file_iyesde(file)->i_write_hint;
}

static inline int iocb_flags(struct file *file);

static inline u16 ki_hint_validate(enum rw_hint hint)
{
	typeof(((struct kiocb *)0)->ki_hint) max_hint = -1;

	if (hint <= max_hint)
		return hint;
	return 0;
}

static inline void init_sync_kiocb(struct kiocb *kiocb, struct file *filp)
{
	*kiocb = (struct kiocb) {
		.ki_filp = filp,
		.ki_flags = iocb_flags(filp),
		.ki_hint = ki_hint_validate(file_write_hint(filp)),
		.ki_ioprio = get_current_ioprio(),
	};
}

/*
 * Iyesde state bits.  Protected by iyesde->i_lock
 *
 * Three bits determine the dirty state of the iyesde, I_DIRTY_SYNC,
 * I_DIRTY_DATASYNC and I_DIRTY_PAGES.
 *
 * Four bits define the lifetime of an iyesde.  Initially, iyesdes are I_NEW,
 * until that flag is cleared.  I_WILL_FREE, I_FREEING and I_CLEAR are set at
 * various stages of removing an iyesde.
 *
 * Two bits are used for locking and completion yestification, I_NEW and I_SYNC.
 *
 * I_DIRTY_SYNC		Iyesde is dirty, but doesn't have to be written on
 *			fdatasync().  i_atime is the usual cause.
 * I_DIRTY_DATASYNC	Data-related iyesde changes pending. We keep track of
 *			these changes separately from I_DIRTY_SYNC so that we
 *			don't have to write iyesde on fdatasync() when only
 *			mtime has changed in it.
 * I_DIRTY_PAGES	Iyesde has dirty pages.  Iyesde itself may be clean.
 * I_NEW		Serves as both a mutex and completion yestification.
 *			New iyesdes set I_NEW.  If two processes both create
 *			the same iyesde, one of them will release its iyesde and
 *			wait for I_NEW to be released before returning.
 *			Iyesdes in I_WILL_FREE, I_FREEING or I_CLEAR state can
 *			also cause waiting on I_NEW, without I_NEW actually
 *			being set.  find_iyesde() uses this to prevent returning
 *			nearly-dead iyesdes.
 * I_WILL_FREE		Must be set when calling write_iyesde_yesw() if i_count
 *			is zero.  I_FREEING must be set when I_WILL_FREE is
 *			cleared.
 * I_FREEING		Set when iyesde is about to be freed but still has dirty
 *			pages or buffers attached or the iyesde itself is still
 *			dirty.
 * I_CLEAR		Added by clear_iyesde().  In this state the iyesde is
 *			clean and can be destroyed.  Iyesde keeps I_FREEING.
 *
 *			Iyesdes that are I_WILL_FREE, I_FREEING or I_CLEAR are
 *			prohibited for many purposes.  iget() must wait for
 *			the iyesde to be completely released, then create it
 *			anew.  Other functions will just igyesre such iyesdes,
 *			if appropriate.  I_NEW is used for waiting.
 *
 * I_SYNC		Writeback of iyesde is running. The bit is set during
 *			data writeback, and cleared with a wakeup on the bit
 *			address once it is done. The bit is also used to pin
 *			the iyesde in memory for flusher thread.
 *
 * I_REFERENCED		Marks the iyesde as recently references on the LRU list.
 *
 * I_DIO_WAKEUP		Never set.  Only used as a key for wait_on_bit().
 *
 * I_WB_SWITCH		Cgroup bdi_writeback switching in progress.  Used to
 *			synchronize competing switching instances and to tell
 *			wb stat updates to grab the i_pages lock.  See
 *			iyesde_switch_wbs_work_fn() for details.
 *
 * I_OVL_INUSE		Used by overlayfs to get exclusive ownership on upper
 *			and work dirs among overlayfs mounts.
 *
 * I_CREATING		New object's iyesde in the middle of setting up.
 *
 * Q: What is the difference between I_WILL_FREE and I_FREEING?
 */
#define I_DIRTY_SYNC		(1 << 0)
#define I_DIRTY_DATASYNC	(1 << 1)
#define I_DIRTY_PAGES		(1 << 2)
#define __I_NEW			3
#define I_NEW			(1 << __I_NEW)
#define I_WILL_FREE		(1 << 4)
#define I_FREEING		(1 << 5)
#define I_CLEAR			(1 << 6)
#define __I_SYNC		7
#define I_SYNC			(1 << __I_SYNC)
#define I_REFERENCED		(1 << 8)
#define __I_DIO_WAKEUP		9
#define I_DIO_WAKEUP		(1 << __I_DIO_WAKEUP)
#define I_LINKABLE		(1 << 10)
#define I_DIRTY_TIME		(1 << 11)
#define __I_DIRTY_TIME_EXPIRED	12
#define I_DIRTY_TIME_EXPIRED	(1 << __I_DIRTY_TIME_EXPIRED)
#define I_WB_SWITCH		(1 << 13)
#define I_OVL_INUSE		(1 << 14)
#define I_CREATING		(1 << 15)

#define I_DIRTY_INODE (I_DIRTY_SYNC | I_DIRTY_DATASYNC)
#define I_DIRTY (I_DIRTY_INODE | I_DIRTY_PAGES)
#define I_DIRTY_ALL (I_DIRTY | I_DIRTY_TIME)

extern void __mark_iyesde_dirty(struct iyesde *, int);
static inline void mark_iyesde_dirty(struct iyesde *iyesde)
{
	__mark_iyesde_dirty(iyesde, I_DIRTY);
}

static inline void mark_iyesde_dirty_sync(struct iyesde *iyesde)
{
	__mark_iyesde_dirty(iyesde, I_DIRTY_SYNC);
}

extern void inc_nlink(struct iyesde *iyesde);
extern void drop_nlink(struct iyesde *iyesde);
extern void clear_nlink(struct iyesde *iyesde);
extern void set_nlink(struct iyesde *iyesde, unsigned int nlink);

static inline void iyesde_inc_link_count(struct iyesde *iyesde)
{
	inc_nlink(iyesde);
	mark_iyesde_dirty(iyesde);
}

static inline void iyesde_dec_link_count(struct iyesde *iyesde)
{
	drop_nlink(iyesde);
	mark_iyesde_dirty(iyesde);
}

enum file_time_flags {
	S_ATIME = 1,
	S_MTIME = 2,
	S_CTIME = 4,
	S_VERSION = 8,
};

extern bool atime_needs_update(const struct path *, struct iyesde *);
extern void touch_atime(const struct path *);
static inline void file_accessed(struct file *file)
{
	if (!(file->f_flags & O_NOATIME))
		touch_atime(&file->f_path);
}

extern int file_modified(struct file *file);

int sync_iyesde(struct iyesde *iyesde, struct writeback_control *wbc);
int sync_iyesde_metadata(struct iyesde *iyesde, int wait);

struct file_system_type {
	const char *name;
	int fs_flags;
#define FS_REQUIRES_DEV		1 
#define FS_BINARY_MOUNTDATA	2
#define FS_HAS_SUBTYPE		4
#define FS_USERNS_MOUNT		8	/* Can be mounted by userns root */
#define FS_DISALLOW_NOTIFY_PERM	16	/* Disable fayestify permission events */
#define FS_RENAME_DOES_D_MOVE	32768	/* FS will handle d_move() during rename() internally. */
	int (*init_fs_context)(struct fs_context *);
	const struct fs_parameter_description *parameters;
	struct dentry *(*mount) (struct file_system_type *, int,
		       const char *, void *);
	void (*kill_sb) (struct super_block *);
	struct module *owner;
	struct file_system_type * next;
	struct hlist_head fs_supers;

	struct lock_class_key s_lock_key;
	struct lock_class_key s_umount_key;
	struct lock_class_key s_vfs_rename_key;
	struct lock_class_key s_writers_key[SB_FREEZE_LEVELS];

	struct lock_class_key i_lock_key;
	struct lock_class_key i_mutex_key;
	struct lock_class_key i_mutex_dir_key;
};

#define MODULE_ALIAS_FS(NAME) MODULE_ALIAS("fs-" NAME)

#ifdef CONFIG_BLOCK
extern struct dentry *mount_bdev(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data,
	int (*fill_super)(struct super_block *, void *, int));
#else
static inline struct dentry *mount_bdev(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data,
	int (*fill_super)(struct super_block *, void *, int))
{
	return ERR_PTR(-ENODEV);
}
#endif
extern struct dentry *mount_single(struct file_system_type *fs_type,
	int flags, void *data,
	int (*fill_super)(struct super_block *, void *, int));
extern struct dentry *mount_yesdev(struct file_system_type *fs_type,
	int flags, void *data,
	int (*fill_super)(struct super_block *, void *, int));
extern struct dentry *mount_subtree(struct vfsmount *mnt, const char *path);
void generic_shutdown_super(struct super_block *sb);
#ifdef CONFIG_BLOCK
void kill_block_super(struct super_block *sb);
#else
static inline void kill_block_super(struct super_block *sb)
{
	BUG();
}
#endif
void kill_ayesn_super(struct super_block *sb);
void kill_litter_super(struct super_block *sb);
void deactivate_super(struct super_block *sb);
void deactivate_locked_super(struct super_block *sb);
int set_ayesn_super(struct super_block *s, void *data);
int set_ayesn_super_fc(struct super_block *s, struct fs_context *fc);
int get_ayesn_bdev(dev_t *);
void free_ayesn_bdev(dev_t);
struct super_block *sget_fc(struct fs_context *fc,
			    int (*test)(struct super_block *, struct fs_context *),
			    int (*set)(struct super_block *, struct fs_context *));
struct super_block *sget(struct file_system_type *type,
			int (*test)(struct super_block *,void *),
			int (*set)(struct super_block *,void *),
			int flags, void *data);

/* Alas, yes aliases. Too much hassle with bringing module.h everywhere */
#define fops_get(fops) \
	(((fops) && try_module_get((fops)->owner) ? (fops) : NULL))
#define fops_put(fops) \
	do { if (fops) module_put((fops)->owner); } while(0)
/*
 * This one is to be used *ONLY* from ->open() instances.
 * fops must be yesn-NULL, pinned down *and* module dependencies
 * should be sufficient to pin the caller down as well.
 */
#define replace_fops(f, fops) \
	do {	\
		struct file *__file = (f); \
		fops_put(__file->f_op); \
		BUG_ON(!(__file->f_op = (fops))); \
	} while(0)

extern int register_filesystem(struct file_system_type *);
extern int unregister_filesystem(struct file_system_type *);
extern struct vfsmount *kern_mount(struct file_system_type *);
extern void kern_unmount(struct vfsmount *mnt);
extern int may_umount_tree(struct vfsmount *);
extern int may_umount(struct vfsmount *);
extern long do_mount(const char *, const char __user *,
		     const char *, unsigned long, void *);
extern struct vfsmount *collect_mounts(const struct path *);
extern void drop_collected_mounts(struct vfsmount *);
extern int iterate_mounts(int (*)(struct vfsmount *, void *), void *,
			  struct vfsmount *);
extern int vfs_statfs(const struct path *, struct kstatfs *);
extern int user_statfs(const char __user *, struct kstatfs *);
extern int fd_statfs(int, struct kstatfs *);
extern int freeze_super(struct super_block *super);
extern int thaw_super(struct super_block *super);
extern bool our_mnt(struct vfsmount *mnt);
extern __printf(2, 3)
int super_setup_bdi_name(struct super_block *sb, char *fmt, ...);
extern int super_setup_bdi(struct super_block *sb);

extern int current_umask(void);

extern void ihold(struct iyesde * iyesde);
extern void iput(struct iyesde *);
extern int generic_update_time(struct iyesde *, struct timespec64 *, int);

/* /sys/fs */
extern struct kobject *fs_kobj;

#define MAX_RW_COUNT (INT_MAX & PAGE_MASK)

#ifdef CONFIG_MANDATORY_FILE_LOCKING
extern int locks_mandatory_locked(struct file *);
extern int locks_mandatory_area(struct iyesde *, struct file *, loff_t, loff_t, unsigned char);

/*
 * Candidates for mandatory locking have the setgid bit set
 * but yes group execute bit -  an otherwise meaningless combination.
 */

static inline int __mandatory_lock(struct iyesde *iyes)
{
	return (iyes->i_mode & (S_ISGID | S_IXGRP)) == S_ISGID;
}

/*
 * ... and these candidates should be on SB_MANDLOCK mounted fs,
 * otherwise these will be advisory locks
 */

static inline int mandatory_lock(struct iyesde *iyes)
{
	return IS_MANDLOCK(iyes) && __mandatory_lock(iyes);
}

static inline int locks_verify_locked(struct file *file)
{
	if (mandatory_lock(locks_iyesde(file)))
		return locks_mandatory_locked(file);
	return 0;
}

static inline int locks_verify_truncate(struct iyesde *iyesde,
				    struct file *f,
				    loff_t size)
{
	if (!iyesde->i_flctx || !mandatory_lock(iyesde))
		return 0;

	if (size < iyesde->i_size) {
		return locks_mandatory_area(iyesde, f, size, iyesde->i_size - 1,
				F_WRLCK);
	} else {
		return locks_mandatory_area(iyesde, f, iyesde->i_size, size - 1,
				F_WRLCK);
	}
}

#else /* !CONFIG_MANDATORY_FILE_LOCKING */

static inline int locks_mandatory_locked(struct file *file)
{
	return 0;
}

static inline int locks_mandatory_area(struct iyesde *iyesde, struct file *filp,
                                       loff_t start, loff_t end, unsigned char type)
{
	return 0;
}

static inline int __mandatory_lock(struct iyesde *iyesde)
{
	return 0;
}

static inline int mandatory_lock(struct iyesde *iyesde)
{
	return 0;
}

static inline int locks_verify_locked(struct file *file)
{
	return 0;
}

static inline int locks_verify_truncate(struct iyesde *iyesde, struct file *filp,
					size_t size)
{
	return 0;
}

#endif /* CONFIG_MANDATORY_FILE_LOCKING */


#ifdef CONFIG_FILE_LOCKING
static inline int break_lease(struct iyesde *iyesde, unsigned int mode)
{
	/*
	 * Since this check is lockless, we must ensure that any refcounts
	 * taken are done before checking i_flctx->flc_lease. Otherwise, we
	 * could end up racing with tasks trying to set a new lease on this
	 * file.
	 */
	smp_mb();
	if (iyesde->i_flctx && !list_empty_careful(&iyesde->i_flctx->flc_lease))
		return __break_lease(iyesde, mode, FL_LEASE);
	return 0;
}

static inline int break_deleg(struct iyesde *iyesde, unsigned int mode)
{
	/*
	 * Since this check is lockless, we must ensure that any refcounts
	 * taken are done before checking i_flctx->flc_lease. Otherwise, we
	 * could end up racing with tasks trying to set a new lease on this
	 * file.
	 */
	smp_mb();
	if (iyesde->i_flctx && !list_empty_careful(&iyesde->i_flctx->flc_lease))
		return __break_lease(iyesde, mode, FL_DELEG);
	return 0;
}

static inline int try_break_deleg(struct iyesde *iyesde, struct iyesde **delegated_iyesde)
{
	int ret;

	ret = break_deleg(iyesde, O_WRONLY|O_NONBLOCK);
	if (ret == -EWOULDBLOCK && delegated_iyesde) {
		*delegated_iyesde = iyesde;
		ihold(iyesde);
	}
	return ret;
}

static inline int break_deleg_wait(struct iyesde **delegated_iyesde)
{
	int ret;

	ret = break_deleg(*delegated_iyesde, O_WRONLY);
	iput(*delegated_iyesde);
	*delegated_iyesde = NULL;
	return ret;
}

static inline int break_layout(struct iyesde *iyesde, bool wait)
{
	smp_mb();
	if (iyesde->i_flctx && !list_empty_careful(&iyesde->i_flctx->flc_lease))
		return __break_lease(iyesde,
				wait ? O_WRONLY : O_WRONLY | O_NONBLOCK,
				FL_LAYOUT);
	return 0;
}

#else /* !CONFIG_FILE_LOCKING */
static inline int break_lease(struct iyesde *iyesde, unsigned int mode)
{
	return 0;
}

static inline int break_deleg(struct iyesde *iyesde, unsigned int mode)
{
	return 0;
}

static inline int try_break_deleg(struct iyesde *iyesde, struct iyesde **delegated_iyesde)
{
	return 0;
}

static inline int break_deleg_wait(struct iyesde **delegated_iyesde)
{
	BUG();
	return 0;
}

static inline int break_layout(struct iyesde *iyesde, bool wait)
{
	return 0;
}

#endif /* CONFIG_FILE_LOCKING */

/* fs/open.c */
struct audit_names;
struct filename {
	const char		*name;	/* pointer to actual string */
	const __user char	*uptr;	/* original userland pointer */
	int			refcnt;
	struct audit_names	*aname;
	const char		iname[];
};
static_assert(offsetof(struct filename, iname) % sizeof(long) == 0);

extern long vfs_truncate(const struct path *, loff_t);
extern int do_truncate(struct dentry *, loff_t start, unsigned int time_attrs,
		       struct file *filp);
extern int vfs_fallocate(struct file *file, int mode, loff_t offset,
			loff_t len);
extern long do_sys_open(int dfd, const char __user *filename, int flags,
			umode_t mode);
extern struct file *file_open_name(struct filename *, int, umode_t);
extern struct file *filp_open(const char *, int, umode_t);
extern struct file *file_open_root(struct dentry *, struct vfsmount *,
				   const char *, int, umode_t);
extern struct file * dentry_open(const struct path *, int, const struct cred *);
extern struct file * open_with_fake_path(const struct path *, int,
					 struct iyesde*, const struct cred *);
static inline struct file *file_clone_open(struct file *file)
{
	return dentry_open(&file->f_path, file->f_flags, file->f_cred);
}
extern int filp_close(struct file *, fl_owner_t id);

extern struct filename *getname_flags(const char __user *, int, int *);
extern struct filename *getname(const char __user *);
extern struct filename *getname_kernel(const char *);
extern void putname(struct filename *name);

extern int finish_open(struct file *file, struct dentry *dentry,
			int (*open)(struct iyesde *, struct file *));
extern int finish_yes_open(struct file *file, struct dentry *dentry);

/* fs/ioctl.c */

extern int ioctl_preallocate(struct file *filp, int mode, void __user *argp);

/* fs/dcache.c */
extern void __init vfs_caches_init_early(void);
extern void __init vfs_caches_init(void);

extern struct kmem_cache *names_cachep;

#define __getname()		kmem_cache_alloc(names_cachep, GFP_KERNEL)
#define __putname(name)		kmem_cache_free(names_cachep, (void *)(name))

#ifdef CONFIG_BLOCK
extern int register_blkdev(unsigned int, const char *);
extern void unregister_blkdev(unsigned int, const char *);
extern void bdev_unhash_iyesde(dev_t dev);
extern struct block_device *bdget(dev_t);
extern struct block_device *bdgrab(struct block_device *bdev);
extern void bd_set_size(struct block_device *, loff_t size);
extern void bd_forget(struct iyesde *iyesde);
extern void bdput(struct block_device *);
extern void invalidate_bdev(struct block_device *);
extern void iterate_bdevs(void (*)(struct block_device *, void *), void *);
extern int sync_blockdev(struct block_device *bdev);
extern void kill_bdev(struct block_device *);
extern struct super_block *freeze_bdev(struct block_device *);
extern void emergency_thaw_all(void);
extern void emergency_thaw_bdev(struct super_block *sb);
extern int thaw_bdev(struct block_device *bdev, struct super_block *sb);
extern int fsync_bdev(struct block_device *);

extern struct super_block *blockdev_superblock;

static inline bool sb_is_blkdev_sb(struct super_block *sb)
{
	return sb == blockdev_superblock;
}
#else
static inline void bd_forget(struct iyesde *iyesde) {}
static inline int sync_blockdev(struct block_device *bdev) { return 0; }
static inline void kill_bdev(struct block_device *bdev) {}
static inline void invalidate_bdev(struct block_device *bdev) {}

static inline struct super_block *freeze_bdev(struct block_device *sb)
{
	return NULL;
}

static inline int thaw_bdev(struct block_device *bdev, struct super_block *sb)
{
	return 0;
}

static inline int emergency_thaw_bdev(struct super_block *sb)
{
	return 0;
}

static inline void iterate_bdevs(void (*f)(struct block_device *, void *), void *arg)
{
}

static inline bool sb_is_blkdev_sb(struct super_block *sb)
{
	return false;
}
#endif
extern int sync_filesystem(struct super_block *);
extern const struct file_operations def_blk_fops;
extern const struct file_operations def_chr_fops;
#ifdef CONFIG_BLOCK
extern int ioctl_by_bdev(struct block_device *, unsigned, unsigned long);
extern int blkdev_ioctl(struct block_device *, fmode_t, unsigned, unsigned long);
extern long compat_blkdev_ioctl(struct file *, unsigned, unsigned long);
extern int blkdev_get(struct block_device *bdev, fmode_t mode, void *holder);
extern struct block_device *blkdev_get_by_path(const char *path, fmode_t mode,
					       void *holder);
extern struct block_device *blkdev_get_by_dev(dev_t dev, fmode_t mode,
					      void *holder);
extern struct block_device *bd_start_claiming(struct block_device *bdev,
					      void *holder);
extern void bd_finish_claiming(struct block_device *bdev,
			       struct block_device *whole, void *holder);
extern void bd_abort_claiming(struct block_device *bdev,
			      struct block_device *whole, void *holder);
extern void blkdev_put(struct block_device *bdev, fmode_t mode);

#ifdef CONFIG_SYSFS
extern int bd_link_disk_holder(struct block_device *bdev, struct gendisk *disk);
extern void bd_unlink_disk_holder(struct block_device *bdev,
				  struct gendisk *disk);
#else
static inline int bd_link_disk_holder(struct block_device *bdev,
				      struct gendisk *disk)
{
	return 0;
}
static inline void bd_unlink_disk_holder(struct block_device *bdev,
					 struct gendisk *disk)
{
}
#endif
#endif

/* fs/char_dev.c */
#define CHRDEV_MAJOR_MAX 512
/* Marks the bottom of the first segment of free char majors */
#define CHRDEV_MAJOR_DYN_END 234
/* Marks the top and bottom of the second segment of free char majors */
#define CHRDEV_MAJOR_DYN_EXT_START 511
#define CHRDEV_MAJOR_DYN_EXT_END 384

extern int alloc_chrdev_region(dev_t *, unsigned, unsigned, const char *);
extern int register_chrdev_region(dev_t, unsigned, const char *);
extern int __register_chrdev(unsigned int major, unsigned int basemiyesr,
			     unsigned int count, const char *name,
			     const struct file_operations *fops);
extern void __unregister_chrdev(unsigned int major, unsigned int basemiyesr,
				unsigned int count, const char *name);
extern void unregister_chrdev_region(dev_t, unsigned);
extern void chrdev_show(struct seq_file *,off_t);

static inline int register_chrdev(unsigned int major, const char *name,
				  const struct file_operations *fops)
{
	return __register_chrdev(major, 0, 256, name, fops);
}

static inline void unregister_chrdev(unsigned int major, const char *name)
{
	__unregister_chrdev(major, 0, 256, name);
}

/* fs/block_dev.c */
#define BDEVNAME_SIZE	32	/* Largest string for a blockdev identifier */
#define BDEVT_SIZE	10	/* Largest string for MAJ:MIN for blkdev */

#ifdef CONFIG_BLOCK
#define BLKDEV_MAJOR_MAX	512
extern const char *__bdevname(dev_t, char *buffer);
extern const char *bdevname(struct block_device *bdev, char *buffer);
extern struct block_device *lookup_bdev(const char *);
extern void blkdev_show(struct seq_file *,off_t);

#else
#define BLKDEV_MAJOR_MAX	0
#endif

extern void init_special_iyesde(struct iyesde *, umode_t, dev_t);

/* Invalid iyesde operations -- fs/bad_iyesde.c */
extern void make_bad_iyesde(struct iyesde *);
extern bool is_bad_iyesde(struct iyesde *);

#ifdef CONFIG_BLOCK
extern int revalidate_disk(struct gendisk *);
extern int check_disk_change(struct block_device *);
extern int __invalidate_device(struct block_device *, bool);
extern int invalidate_partition(struct gendisk *, int);
#endif
unsigned long invalidate_mapping_pages(struct address_space *mapping,
					pgoff_t start, pgoff_t end);

static inline void invalidate_remote_iyesde(struct iyesde *iyesde)
{
	if (S_ISREG(iyesde->i_mode) || S_ISDIR(iyesde->i_mode) ||
	    S_ISLNK(iyesde->i_mode))
		invalidate_mapping_pages(iyesde->i_mapping, 0, -1);
}
extern int invalidate_iyesde_pages2(struct address_space *mapping);
extern int invalidate_iyesde_pages2_range(struct address_space *mapping,
					 pgoff_t start, pgoff_t end);
extern int write_iyesde_yesw(struct iyesde *, int);
extern int filemap_fdatawrite(struct address_space *);
extern int filemap_flush(struct address_space *);
extern int filemap_fdatawait_keep_errors(struct address_space *mapping);
extern int filemap_fdatawait_range(struct address_space *, loff_t lstart,
				   loff_t lend);
extern int filemap_fdatawait_range_keep_errors(struct address_space *mapping,
		loff_t start_byte, loff_t end_byte);

static inline int filemap_fdatawait(struct address_space *mapping)
{
	return filemap_fdatawait_range(mapping, 0, LLONG_MAX);
}

extern bool filemap_range_has_page(struct address_space *, loff_t lstart,
				  loff_t lend);
extern int filemap_write_and_wait(struct address_space *mapping);
extern int filemap_write_and_wait_range(struct address_space *mapping,
				        loff_t lstart, loff_t lend);
extern int __filemap_fdatawrite_range(struct address_space *mapping,
				loff_t start, loff_t end, int sync_mode);
extern int filemap_fdatawrite_range(struct address_space *mapping,
				loff_t start, loff_t end);
extern int filemap_check_errors(struct address_space *mapping);
extern void __filemap_set_wb_err(struct address_space *mapping, int err);

extern int __must_check file_fdatawait_range(struct file *file, loff_t lstart,
						loff_t lend);
extern int __must_check file_check_and_advance_wb_err(struct file *file);
extern int __must_check file_write_and_wait_range(struct file *file,
						loff_t start, loff_t end);

static inline int file_write_and_wait(struct file *file)
{
	return file_write_and_wait_range(file, 0, LLONG_MAX);
}

/**
 * filemap_set_wb_err - set a writeback error on an address_space
 * @mapping: mapping in which to set writeback error
 * @err: error to be set in mapping
 *
 * When writeback fails in some way, we must record that error so that
 * userspace can be informed when fsync and the like are called.  We endeavor
 * to report errors on any file that was open at the time of the error.  Some
 * internal callers also need to kyesw when writeback errors have occurred.
 *
 * When a writeback error occurs, most filesystems will want to call
 * filemap_set_wb_err to record the error in the mapping so that it will be
 * automatically reported whenever fsync is called on the file.
 */
static inline void filemap_set_wb_err(struct address_space *mapping, int err)
{
	/* Fastpath for common case of yes error */
	if (unlikely(err))
		__filemap_set_wb_err(mapping, err);
}

/**
 * filemap_check_wb_error - has an error occurred since the mark was sampled?
 * @mapping: mapping to check for writeback errors
 * @since: previously-sampled errseq_t
 *
 * Grab the errseq_t value from the mapping, and see if it has changed "since"
 * the given value was sampled.
 *
 * If it has then report the latest error set, otherwise return 0.
 */
static inline int filemap_check_wb_err(struct address_space *mapping,
					errseq_t since)
{
	return errseq_check(&mapping->wb_err, since);
}

/**
 * filemap_sample_wb_err - sample the current errseq_t to test for later errors
 * @mapping: mapping to be sampled
 *
 * Writeback errors are always reported relative to a particular sample point
 * in the past. This function provides those sample points.
 */
static inline errseq_t filemap_sample_wb_err(struct address_space *mapping)
{
	return errseq_sample(&mapping->wb_err);
}

static inline int filemap_nr_thps(struct address_space *mapping)
{
#ifdef CONFIG_READ_ONLY_THP_FOR_FS
	return atomic_read(&mapping->nr_thps);
#else
	return 0;
#endif
}

static inline void filemap_nr_thps_inc(struct address_space *mapping)
{
#ifdef CONFIG_READ_ONLY_THP_FOR_FS
	atomic_inc(&mapping->nr_thps);
#else
	WARN_ON_ONCE(1);
#endif
}

static inline void filemap_nr_thps_dec(struct address_space *mapping)
{
#ifdef CONFIG_READ_ONLY_THP_FOR_FS
	atomic_dec(&mapping->nr_thps);
#else
	WARN_ON_ONCE(1);
#endif
}

extern int vfs_fsync_range(struct file *file, loff_t start, loff_t end,
			   int datasync);
extern int vfs_fsync(struct file *file, int datasync);

extern int sync_file_range(struct file *file, loff_t offset, loff_t nbytes,
				unsigned int flags);

/*
 * Sync the bytes written if this was a synchroyesus write.  Expect ki_pos
 * to already be updated for the write, and will return either the amount
 * of bytes passed in, or an error if syncing the file failed.
 */
static inline ssize_t generic_write_sync(struct kiocb *iocb, ssize_t count)
{
	if (iocb->ki_flags & IOCB_DSYNC) {
		int ret = vfs_fsync_range(iocb->ki_filp,
				iocb->ki_pos - count, iocb->ki_pos - 1,
				(iocb->ki_flags & IOCB_SYNC) ? 0 : 1);
		if (ret)
			return ret;
	}

	return count;
}

extern void emergency_sync(void);
extern void emergency_remount(void);
#ifdef CONFIG_BLOCK
extern sector_t bmap(struct iyesde *, sector_t);
#endif
extern int yestify_change(struct dentry *, struct iattr *, struct iyesde **);
extern int iyesde_permission(struct iyesde *, int);
extern int generic_permission(struct iyesde *, int);
extern int __check_sticky(struct iyesde *dir, struct iyesde *iyesde);

static inline bool execute_ok(struct iyesde *iyesde)
{
	return (iyesde->i_mode & S_IXUGO) || S_ISDIR(iyesde->i_mode);
}

static inline void file_start_write(struct file *file)
{
	if (!S_ISREG(file_iyesde(file)->i_mode))
		return;
	__sb_start_write(file_iyesde(file)->i_sb, SB_FREEZE_WRITE, true);
}

static inline bool file_start_write_trylock(struct file *file)
{
	if (!S_ISREG(file_iyesde(file)->i_mode))
		return true;
	return __sb_start_write(file_iyesde(file)->i_sb, SB_FREEZE_WRITE, false);
}

static inline void file_end_write(struct file *file)
{
	if (!S_ISREG(file_iyesde(file)->i_mode))
		return;
	__sb_end_write(file_iyesde(file)->i_sb, SB_FREEZE_WRITE);
}

/*
 * get_write_access() gets write permission for a file.
 * put_write_access() releases this write permission.
 * This is used for regular files.
 * We canyest support write (and maybe mmap read-write shared) accesses and
 * MAP_DENYWRITE mmappings simultaneously. The i_writecount field of an iyesde
 * can have the following values:
 * 0: yes writers, yes VM_DENYWRITE mappings
 * < 0: (-i_writecount) vm_area_structs with VM_DENYWRITE set exist
 * > 0: (i_writecount) users are writing to the file.
 *
 * Normally we operate on that counter with atomic_{inc,dec} and it's safe
 * except for the cases where we don't hold i_writecount yet. Then we need to
 * use {get,deny}_write_access() - these functions check the sign and refuse
 * to do the change if sign is wrong.
 */
static inline int get_write_access(struct iyesde *iyesde)
{
	return atomic_inc_unless_negative(&iyesde->i_writecount) ? 0 : -ETXTBSY;
}
static inline int deny_write_access(struct file *file)
{
	struct iyesde *iyesde = file_iyesde(file);
	return atomic_dec_unless_positive(&iyesde->i_writecount) ? 0 : -ETXTBSY;
}
static inline void put_write_access(struct iyesde * iyesde)
{
	atomic_dec(&iyesde->i_writecount);
}
static inline void allow_write_access(struct file *file)
{
	if (file)
		atomic_inc(&file_iyesde(file)->i_writecount);
}
static inline bool iyesde_is_open_for_write(const struct iyesde *iyesde)
{
	return atomic_read(&iyesde->i_writecount) > 0;
}

#if defined(CONFIG_IMA) || defined(CONFIG_FILE_LOCKING)
static inline void i_readcount_dec(struct iyesde *iyesde)
{
	BUG_ON(!atomic_read(&iyesde->i_readcount));
	atomic_dec(&iyesde->i_readcount);
}
static inline void i_readcount_inc(struct iyesde *iyesde)
{
	atomic_inc(&iyesde->i_readcount);
}
#else
static inline void i_readcount_dec(struct iyesde *iyesde)
{
	return;
}
static inline void i_readcount_inc(struct iyesde *iyesde)
{
	return;
}
#endif
extern int do_pipe_flags(int *, int);

#define __kernel_read_file_id(id) \
	id(UNKNOWN, unkyeswn)		\
	id(FIRMWARE, firmware)		\
	id(FIRMWARE_PREALLOC_BUFFER, firmware)	\
	id(MODULE, kernel-module)		\
	id(KEXEC_IMAGE, kexec-image)		\
	id(KEXEC_INITRAMFS, kexec-initramfs)	\
	id(POLICY, security-policy)		\
	id(X509_CERTIFICATE, x509-certificate)	\
	id(MAX_ID, )

#define __fid_enumify(ENUM, dummy) READING_ ## ENUM,
#define __fid_stringify(dummy, str) #str,

enum kernel_read_file_id {
	__kernel_read_file_id(__fid_enumify)
};

static const char * const kernel_read_file_str[] = {
	__kernel_read_file_id(__fid_stringify)
};

static inline const char *kernel_read_file_id_str(enum kernel_read_file_id id)
{
	if ((unsigned)id >= READING_MAX_ID)
		return kernel_read_file_str[READING_UNKNOWN];

	return kernel_read_file_str[id];
}

extern int kernel_read_file(struct file *, void **, loff_t *, loff_t,
			    enum kernel_read_file_id);
extern int kernel_read_file_from_path(const char *, void **, loff_t *, loff_t,
				      enum kernel_read_file_id);
extern int kernel_read_file_from_fd(int, void **, loff_t *, loff_t,
				    enum kernel_read_file_id);
extern ssize_t kernel_read(struct file *, void *, size_t, loff_t *);
extern ssize_t kernel_write(struct file *, const void *, size_t, loff_t *);
extern ssize_t __kernel_write(struct file *, const void *, size_t, loff_t *);
extern struct file * open_exec(const char *);
 
/* fs/dcache.c -- generic fs support functions */
extern bool is_subdir(struct dentry *, struct dentry *);
extern bool path_is_under(const struct path *, const struct path *);

extern char *file_path(struct file *, char *, int);

#include <linux/err.h>

/* needed for stackable file system support */
extern loff_t default_llseek(struct file *file, loff_t offset, int whence);

extern loff_t vfs_llseek(struct file *file, loff_t offset, int whence);

extern int iyesde_init_always(struct super_block *, struct iyesde *);
extern void iyesde_init_once(struct iyesde *);
extern void address_space_init_once(struct address_space *mapping);
extern struct iyesde * igrab(struct iyesde *);
extern iyes_t iunique(struct super_block *, iyes_t);
extern int iyesde_needs_sync(struct iyesde *iyesde);
extern int generic_delete_iyesde(struct iyesde *iyesde);
static inline int generic_drop_iyesde(struct iyesde *iyesde)
{
	return !iyesde->i_nlink || iyesde_unhashed(iyesde);
}

extern struct iyesde *ilookup5_yeswait(struct super_block *sb,
		unsigned long hashval, int (*test)(struct iyesde *, void *),
		void *data);
extern struct iyesde *ilookup5(struct super_block *sb, unsigned long hashval,
		int (*test)(struct iyesde *, void *), void *data);
extern struct iyesde *ilookup(struct super_block *sb, unsigned long iyes);

extern struct iyesde *iyesde_insert5(struct iyesde *iyesde, unsigned long hashval,
		int (*test)(struct iyesde *, void *),
		int (*set)(struct iyesde *, void *),
		void *data);
extern struct iyesde * iget5_locked(struct super_block *, unsigned long, int (*test)(struct iyesde *, void *), int (*set)(struct iyesde *, void *), void *);
extern struct iyesde * iget_locked(struct super_block *, unsigned long);
extern struct iyesde *find_iyesde_yeswait(struct super_block *,
				       unsigned long,
				       int (*match)(struct iyesde *,
						    unsigned long, void *),
				       void *data);
extern int insert_iyesde_locked4(struct iyesde *, unsigned long, int (*test)(struct iyesde *, void *), void *);
extern int insert_iyesde_locked(struct iyesde *);
#ifdef CONFIG_DEBUG_LOCK_ALLOC
extern void lockdep_anyestate_iyesde_mutex_key(struct iyesde *iyesde);
#else
static inline void lockdep_anyestate_iyesde_mutex_key(struct iyesde *iyesde) { };
#endif
extern void unlock_new_iyesde(struct iyesde *);
extern void discard_new_iyesde(struct iyesde *);
extern unsigned int get_next_iyes(void);
extern void evict_iyesdes(struct super_block *sb);

extern void __iget(struct iyesde * iyesde);
extern void iget_failed(struct iyesde *);
extern void clear_iyesde(struct iyesde *);
extern void __destroy_iyesde(struct iyesde *);
extern struct iyesde *new_iyesde_pseudo(struct super_block *sb);
extern struct iyesde *new_iyesde(struct super_block *sb);
extern void free_iyesde_yesnrcu(struct iyesde *iyesde);
extern int should_remove_suid(struct dentry *);
extern int file_remove_privs(struct file *);

extern void __insert_iyesde_hash(struct iyesde *, unsigned long hashval);
static inline void insert_iyesde_hash(struct iyesde *iyesde)
{
	__insert_iyesde_hash(iyesde, iyesde->i_iyes);
}

extern void __remove_iyesde_hash(struct iyesde *);
static inline void remove_iyesde_hash(struct iyesde *iyesde)
{
	if (!iyesde_unhashed(iyesde) && !hlist_fake(&iyesde->i_hash))
		__remove_iyesde_hash(iyesde);
}

extern void iyesde_sb_list_add(struct iyesde *iyesde);

#ifdef CONFIG_BLOCK
extern int bdev_read_only(struct block_device *);
#endif
extern int set_blocksize(struct block_device *, int);
extern int sb_set_blocksize(struct super_block *, int);
extern int sb_min_blocksize(struct super_block *, int);

extern int generic_file_mmap(struct file *, struct vm_area_struct *);
extern int generic_file_readonly_mmap(struct file *, struct vm_area_struct *);
extern ssize_t generic_write_checks(struct kiocb *, struct iov_iter *);
extern int generic_remap_checks(struct file *file_in, loff_t pos_in,
				struct file *file_out, loff_t pos_out,
				loff_t *count, unsigned int remap_flags);
extern int generic_file_rw_checks(struct file *file_in, struct file *file_out);
extern int generic_copy_file_checks(struct file *file_in, loff_t pos_in,
				    struct file *file_out, loff_t pos_out,
				    size_t *count, unsigned int flags);
extern ssize_t generic_file_read_iter(struct kiocb *, struct iov_iter *);
extern ssize_t __generic_file_write_iter(struct kiocb *, struct iov_iter *);
extern ssize_t generic_file_write_iter(struct kiocb *, struct iov_iter *);
extern ssize_t generic_file_direct_write(struct kiocb *, struct iov_iter *);
extern ssize_t generic_perform_write(struct file *, struct iov_iter *, loff_t);

ssize_t vfs_iter_read(struct file *file, struct iov_iter *iter, loff_t *ppos,
		rwf_t flags);
ssize_t vfs_iter_write(struct file *file, struct iov_iter *iter, loff_t *ppos,
		rwf_t flags);

/* fs/block_dev.c */
extern ssize_t blkdev_read_iter(struct kiocb *iocb, struct iov_iter *to);
extern ssize_t blkdev_write_iter(struct kiocb *iocb, struct iov_iter *from);
extern int blkdev_fsync(struct file *filp, loff_t start, loff_t end,
			int datasync);
extern void block_sync_page(struct page *page);

/* fs/splice.c */
extern ssize_t generic_file_splice_read(struct file *, loff_t *,
		struct pipe_iyesde_info *, size_t, unsigned int);
extern ssize_t iter_file_splice_write(struct pipe_iyesde_info *,
		struct file *, loff_t *, size_t, unsigned int);
extern ssize_t generic_splice_sendpage(struct pipe_iyesde_info *pipe,
		struct file *out, loff_t *, size_t len, unsigned int flags);
extern long do_splice_direct(struct file *in, loff_t *ppos, struct file *out,
		loff_t *opos, size_t len, unsigned int flags);


extern void
file_ra_state_init(struct file_ra_state *ra, struct address_space *mapping);
extern loff_t yesop_llseek(struct file *file, loff_t offset, int whence);
extern loff_t yes_llseek(struct file *file, loff_t offset, int whence);
extern loff_t vfs_setpos(struct file *file, loff_t offset, loff_t maxsize);
extern loff_t generic_file_llseek(struct file *file, loff_t offset, int whence);
extern loff_t generic_file_llseek_size(struct file *file, loff_t offset,
		int whence, loff_t maxsize, loff_t eof);
extern loff_t fixed_size_llseek(struct file *file, loff_t offset,
		int whence, loff_t size);
extern loff_t yes_seek_end_llseek_size(struct file *, loff_t, int, loff_t);
extern loff_t yes_seek_end_llseek(struct file *, loff_t, int);
extern int generic_file_open(struct iyesde * iyesde, struct file * filp);
extern int yesnseekable_open(struct iyesde * iyesde, struct file * filp);
extern int stream_open(struct iyesde * iyesde, struct file * filp);

#ifdef CONFIG_BLOCK
typedef void (dio_submit_t)(struct bio *bio, struct iyesde *iyesde,
			    loff_t file_offset);

enum {
	/* need locking between buffered and direct access */
	DIO_LOCKING	= 0x01,

	/* filesystem does yest support filling holes */
	DIO_SKIP_HOLES	= 0x02,
};

void dio_end_io(struct bio *bio);

ssize_t __blockdev_direct_IO(struct kiocb *iocb, struct iyesde *iyesde,
			     struct block_device *bdev, struct iov_iter *iter,
			     get_block_t get_block,
			     dio_iodone_t end_io, dio_submit_t submit_io,
			     int flags);

static inline ssize_t blockdev_direct_IO(struct kiocb *iocb,
					 struct iyesde *iyesde,
					 struct iov_iter *iter,
					 get_block_t get_block)
{
	return __blockdev_direct_IO(iocb, iyesde, iyesde->i_sb->s_bdev, iter,
			get_block, NULL, NULL, DIO_LOCKING | DIO_SKIP_HOLES);
}
#endif

void iyesde_dio_wait(struct iyesde *iyesde);

/*
 * iyesde_dio_begin - signal start of a direct I/O requests
 * @iyesde: iyesde the direct I/O happens on
 *
 * This is called once we've finished processing a direct I/O request,
 * and is used to wake up callers waiting for direct I/O to be quiesced.
 */
static inline void iyesde_dio_begin(struct iyesde *iyesde)
{
	atomic_inc(&iyesde->i_dio_count);
}

/*
 * iyesde_dio_end - signal finish of a direct I/O requests
 * @iyesde: iyesde the direct I/O happens on
 *
 * This is called once we've finished processing a direct I/O request,
 * and is used to wake up callers waiting for direct I/O to be quiesced.
 */
static inline void iyesde_dio_end(struct iyesde *iyesde)
{
	if (atomic_dec_and_test(&iyesde->i_dio_count))
		wake_up_bit(&iyesde->i_state, __I_DIO_WAKEUP);
}

/*
 * Warn about a page cache invalidation failure diring a direct I/O write.
 */
void dio_warn_stale_pagecache(struct file *filp);

extern void iyesde_set_flags(struct iyesde *iyesde, unsigned int flags,
			    unsigned int mask);

extern const struct file_operations generic_ro_fops;

#define special_file(m) (S_ISCHR(m)||S_ISBLK(m)||S_ISFIFO(m)||S_ISSOCK(m))

extern int readlink_copy(char __user *, int, const char *);
extern int page_readlink(struct dentry *, char __user *, int);
extern const char *page_get_link(struct dentry *, struct iyesde *,
				 struct delayed_call *);
extern void page_put_link(void *);
extern int __page_symlink(struct iyesde *iyesde, const char *symname, int len,
		int yesfs);
extern int page_symlink(struct iyesde *iyesde, const char *symname, int len);
extern const struct iyesde_operations page_symlink_iyesde_operations;
extern void kfree_link(void *);
extern void generic_fillattr(struct iyesde *, struct kstat *);
extern int vfs_getattr_yessec(const struct path *, struct kstat *, u32, unsigned int);
extern int vfs_getattr(const struct path *, struct kstat *, u32, unsigned int);
void __iyesde_add_bytes(struct iyesde *iyesde, loff_t bytes);
void iyesde_add_bytes(struct iyesde *iyesde, loff_t bytes);
void __iyesde_sub_bytes(struct iyesde *iyesde, loff_t bytes);
void iyesde_sub_bytes(struct iyesde *iyesde, loff_t bytes);
static inline loff_t __iyesde_get_bytes(struct iyesde *iyesde)
{
	return (((loff_t)iyesde->i_blocks) << 9) + iyesde->i_bytes;
}
loff_t iyesde_get_bytes(struct iyesde *iyesde);
void iyesde_set_bytes(struct iyesde *iyesde, loff_t bytes);
const char *simple_get_link(struct dentry *, struct iyesde *,
			    struct delayed_call *);
extern const struct iyesde_operations simple_symlink_iyesde_operations;

extern int iterate_dir(struct file *, struct dir_context *);

extern int vfs_statx(int, const char __user *, int, struct kstat *, u32);
extern int vfs_statx_fd(unsigned int, struct kstat *, u32, unsigned int);

static inline int vfs_stat(const char __user *filename, struct kstat *stat)
{
	return vfs_statx(AT_FDCWD, filename, AT_NO_AUTOMOUNT,
			 stat, STATX_BASIC_STATS);
}
static inline int vfs_lstat(const char __user *name, struct kstat *stat)
{
	return vfs_statx(AT_FDCWD, name, AT_SYMLINK_NOFOLLOW | AT_NO_AUTOMOUNT,
			 stat, STATX_BASIC_STATS);
}
static inline int vfs_fstatat(int dfd, const char __user *filename,
			      struct kstat *stat, int flags)
{
	return vfs_statx(dfd, filename, flags | AT_NO_AUTOMOUNT,
			 stat, STATX_BASIC_STATS);
}
static inline int vfs_fstat(int fd, struct kstat *stat)
{
	return vfs_statx_fd(fd, stat, STATX_BASIC_STATS, 0);
}


extern const char *vfs_get_link(struct dentry *, struct delayed_call *);
extern int vfs_readlink(struct dentry *, char __user *, int);

extern int __generic_block_fiemap(struct iyesde *iyesde,
				  struct fiemap_extent_info *fieinfo,
				  loff_t start, loff_t len,
				  get_block_t *get_block);
extern int generic_block_fiemap(struct iyesde *iyesde,
				struct fiemap_extent_info *fieinfo, u64 start,
				u64 len, get_block_t *get_block);

extern struct file_system_type *get_filesystem(struct file_system_type *fs);
extern void put_filesystem(struct file_system_type *fs);
extern struct file_system_type *get_fs_type(const char *name);
extern struct super_block *get_super(struct block_device *);
extern struct super_block *get_super_thawed(struct block_device *);
extern struct super_block *get_super_exclusive_thawed(struct block_device *bdev);
extern struct super_block *get_active_super(struct block_device *bdev);
extern void drop_super(struct super_block *sb);
extern void drop_super_exclusive(struct super_block *sb);
extern void iterate_supers(void (*)(struct super_block *, void *), void *);
extern void iterate_supers_type(struct file_system_type *,
			        void (*)(struct super_block *, void *), void *);

extern int dcache_dir_open(struct iyesde *, struct file *);
extern int dcache_dir_close(struct iyesde *, struct file *);
extern loff_t dcache_dir_lseek(struct file *, loff_t, int);
extern int dcache_readdir(struct file *, struct dir_context *);
extern int simple_setattr(struct dentry *, struct iattr *);
extern int simple_getattr(const struct path *, struct kstat *, u32, unsigned int);
extern int simple_statfs(struct dentry *, struct kstatfs *);
extern int simple_open(struct iyesde *iyesde, struct file *file);
extern int simple_link(struct dentry *, struct iyesde *, struct dentry *);
extern int simple_unlink(struct iyesde *, struct dentry *);
extern int simple_rmdir(struct iyesde *, struct dentry *);
extern int simple_rename(struct iyesde *, struct dentry *,
			 struct iyesde *, struct dentry *, unsigned int);
extern int yesop_fsync(struct file *, loff_t, loff_t, int);
extern int yesop_set_page_dirty(struct page *page);
extern void yesop_invalidatepage(struct page *page, unsigned int offset,
		unsigned int length);
extern ssize_t yesop_direct_IO(struct kiocb *iocb, struct iov_iter *iter);
extern int simple_empty(struct dentry *);
extern int simple_readpage(struct file *file, struct page *page);
extern int simple_write_begin(struct file *file, struct address_space *mapping,
			loff_t pos, unsigned len, unsigned flags,
			struct page **pagep, void **fsdata);
extern int simple_write_end(struct file *file, struct address_space *mapping,
			loff_t pos, unsigned len, unsigned copied,
			struct page *page, void *fsdata);
extern int always_delete_dentry(const struct dentry *);
extern struct iyesde *alloc_ayesn_iyesde(struct super_block *);
extern int simple_yessetlease(struct file *, long, struct file_lock **, void **);
extern const struct dentry_operations simple_dentry_operations;

extern struct dentry *simple_lookup(struct iyesde *, struct dentry *, unsigned int flags);
extern ssize_t generic_read_dir(struct file *, char __user *, size_t, loff_t *);
extern const struct file_operations simple_dir_operations;
extern const struct iyesde_operations simple_dir_iyesde_operations;
extern void make_empty_dir_iyesde(struct iyesde *iyesde);
extern bool is_empty_dir_iyesde(struct iyesde *iyesde);
struct tree_descr { const char *name; const struct file_operations *ops; int mode; };
struct dentry *d_alloc_name(struct dentry *, const char *);
extern int simple_fill_super(struct super_block *, unsigned long,
			     const struct tree_descr *);
extern int simple_pin_fs(struct file_system_type *, struct vfsmount **mount, int *count);
extern void simple_release_fs(struct vfsmount **mount, int *count);

extern ssize_t simple_read_from_buffer(void __user *to, size_t count,
			loff_t *ppos, const void *from, size_t available);
extern ssize_t simple_write_to_buffer(void *to, size_t available, loff_t *ppos,
		const void __user *from, size_t count);

extern int __generic_file_fsync(struct file *, loff_t, loff_t, int);
extern int generic_file_fsync(struct file *, loff_t, loff_t, int);

extern int generic_check_addressable(unsigned, u64);

#ifdef CONFIG_MIGRATION
extern int buffer_migrate_page(struct address_space *,
				struct page *, struct page *,
				enum migrate_mode);
extern int buffer_migrate_page_yesrefs(struct address_space *,
				struct page *, struct page *,
				enum migrate_mode);
#else
#define buffer_migrate_page NULL
#define buffer_migrate_page_yesrefs NULL
#endif

extern int setattr_prepare(struct dentry *, struct iattr *);
extern int iyesde_newsize_ok(const struct iyesde *, loff_t offset);
extern void setattr_copy(struct iyesde *iyesde, const struct iattr *attr);

extern int file_update_time(struct file *file);

static inline bool io_is_direct(struct file *filp)
{
	return (filp->f_flags & O_DIRECT) || IS_DAX(filp->f_mapping->host);
}

static inline bool vma_is_dax(struct vm_area_struct *vma)
{
	return vma->vm_file && IS_DAX(vma->vm_file->f_mapping->host);
}

static inline bool vma_is_fsdax(struct vm_area_struct *vma)
{
	struct iyesde *iyesde;

	if (!vma->vm_file)
		return false;
	if (!vma_is_dax(vma))
		return false;
	iyesde = file_iyesde(vma->vm_file);
	if (S_ISCHR(iyesde->i_mode))
		return false; /* device-dax */
	return true;
}

static inline int iocb_flags(struct file *file)
{
	int res = 0;
	if (file->f_flags & O_APPEND)
		res |= IOCB_APPEND;
	if (io_is_direct(file))
		res |= IOCB_DIRECT;
	if ((file->f_flags & O_DSYNC) || IS_SYNC(file->f_mapping->host))
		res |= IOCB_DSYNC;
	if (file->f_flags & __O_SYNC)
		res |= IOCB_SYNC;
	return res;
}

static inline int kiocb_set_rw_flags(struct kiocb *ki, rwf_t flags)
{
	if (unlikely(flags & ~RWF_SUPPORTED))
		return -EOPNOTSUPP;

	if (flags & RWF_NOWAIT) {
		if (!(ki->ki_filp->f_mode & FMODE_NOWAIT))
			return -EOPNOTSUPP;
		ki->ki_flags |= IOCB_NOWAIT;
	}
	if (flags & RWF_HIPRI)
		ki->ki_flags |= IOCB_HIPRI;
	if (flags & RWF_DSYNC)
		ki->ki_flags |= IOCB_DSYNC;
	if (flags & RWF_SYNC)
		ki->ki_flags |= (IOCB_DSYNC | IOCB_SYNC);
	if (flags & RWF_APPEND)
		ki->ki_flags |= IOCB_APPEND;
	return 0;
}

static inline iyes_t parent_iyes(struct dentry *dentry)
{
	iyes_t res;

	/*
	 * Don't strictly need d_lock here? If the parent iyes could change
	 * then surely we'd have a deeper race in the caller?
	 */
	spin_lock(&dentry->d_lock);
	res = dentry->d_parent->d_iyesde->i_iyes;
	spin_unlock(&dentry->d_lock);
	return res;
}

/* Transaction based IO helpers */

/*
 * An argresp is stored in an allocated page and holds the
 * size of the argument or response, along with its content
 */
struct simple_transaction_argresp {
	ssize_t size;
	char data[0];
};

#define SIMPLE_TRANSACTION_LIMIT (PAGE_SIZE - sizeof(struct simple_transaction_argresp))

char *simple_transaction_get(struct file *file, const char __user *buf,
				size_t size);
ssize_t simple_transaction_read(struct file *file, char __user *buf,
				size_t size, loff_t *pos);
int simple_transaction_release(struct iyesde *iyesde, struct file *file);

void simple_transaction_set(struct file *file, size_t n);

/*
 * simple attribute files
 *
 * These attributes behave similar to those in sysfs:
 *
 * Writing to an attribute immediately sets a value, an open file can be
 * written to multiple times.
 *
 * Reading from an attribute creates a buffer from the value that might get
 * read with multiple read calls. When the attribute has been read
 * completely, yes further read calls are possible until the file is opened
 * again.
 *
 * All attributes contain a text representation of a numeric value
 * that are accessed with the get() and set() functions.
 */
#define DEFINE_SIMPLE_ATTRIBUTE(__fops, __get, __set, __fmt)		\
static int __fops ## _open(struct iyesde *iyesde, struct file *file)	\
{									\
	__simple_attr_check_format(__fmt, 0ull);			\
	return simple_attr_open(iyesde, file, __get, __set, __fmt);	\
}									\
static const struct file_operations __fops = {				\
	.owner	 = THIS_MODULE,						\
	.open	 = __fops ## _open,					\
	.release = simple_attr_release,					\
	.read	 = simple_attr_read,					\
	.write	 = simple_attr_write,					\
	.llseek	 = generic_file_llseek,					\
}

static inline __printf(1, 2)
void __simple_attr_check_format(const char *fmt, ...)
{
	/* don't do anything, just let the compiler check the arguments; */
}

int simple_attr_open(struct iyesde *iyesde, struct file *file,
		     int (*get)(void *, u64 *), int (*set)(void *, u64),
		     const char *fmt);
int simple_attr_release(struct iyesde *iyesde, struct file *file);
ssize_t simple_attr_read(struct file *file, char __user *buf,
			 size_t len, loff_t *ppos);
ssize_t simple_attr_write(struct file *file, const char __user *buf,
			  size_t len, loff_t *ppos);

struct ctl_table;
int proc_nr_files(struct ctl_table *table, int write,
		  void __user *buffer, size_t *lenp, loff_t *ppos);
int proc_nr_dentry(struct ctl_table *table, int write,
		  void __user *buffer, size_t *lenp, loff_t *ppos);
int proc_nr_iyesdes(struct ctl_table *table, int write,
		   void __user *buffer, size_t *lenp, loff_t *ppos);
int __init get_filesystem_list(char *buf);

#define __FMODE_EXEC		((__force int) FMODE_EXEC)
#define __FMODE_NONOTIFY	((__force int) FMODE_NONOTIFY)

#define ACC_MODE(x) ("\004\002\006\006"[(x)&O_ACCMODE])
#define OPEN_FMODE(flag) ((__force fmode_t)(((flag + 1) & O_ACCMODE) | \
					    (flag & __FMODE_NONOTIFY)))

static inline bool is_sxid(umode_t mode)
{
	return (mode & S_ISUID) || ((mode & S_ISGID) && (mode & S_IXGRP));
}

static inline int check_sticky(struct iyesde *dir, struct iyesde *iyesde)
{
	if (!(dir->i_mode & S_ISVTX))
		return 0;

	return __check_sticky(dir, iyesde);
}

static inline void iyesde_has_yes_xattr(struct iyesde *iyesde)
{
	if (!is_sxid(iyesde->i_mode) && (iyesde->i_sb->s_flags & SB_NOSEC))
		iyesde->i_flags |= S_NOSEC;
}

static inline bool is_root_iyesde(struct iyesde *iyesde)
{
	return iyesde == iyesde->i_sb->s_root->d_iyesde;
}

static inline bool dir_emit(struct dir_context *ctx,
			    const char *name, int namelen,
			    u64 iyes, unsigned type)
{
	return ctx->actor(ctx, name, namelen, ctx->pos, iyes, type) == 0;
}
static inline bool dir_emit_dot(struct file *file, struct dir_context *ctx)
{
	return ctx->actor(ctx, ".", 1, ctx->pos,
			  file->f_path.dentry->d_iyesde->i_iyes, DT_DIR) == 0;
}
static inline bool dir_emit_dotdot(struct file *file, struct dir_context *ctx)
{
	return ctx->actor(ctx, "..", 2, ctx->pos,
			  parent_iyes(file->f_path.dentry), DT_DIR) == 0;
}
static inline bool dir_emit_dots(struct file *file, struct dir_context *ctx)
{
	if (ctx->pos == 0) {
		if (!dir_emit_dot(file, ctx))
			return false;
		ctx->pos = 1;
	}
	if (ctx->pos == 1) {
		if (!dir_emit_dotdot(file, ctx))
			return false;
		ctx->pos = 2;
	}
	return true;
}
static inline bool dir_relax(struct iyesde *iyesde)
{
	iyesde_unlock(iyesde);
	iyesde_lock(iyesde);
	return !IS_DEADDIR(iyesde);
}

static inline bool dir_relax_shared(struct iyesde *iyesde)
{
	iyesde_unlock_shared(iyesde);
	iyesde_lock_shared(iyesde);
	return !IS_DEADDIR(iyesde);
}

extern bool path_yesexec(const struct path *path);
extern void iyesde_yeshighmem(struct iyesde *iyesde);

/* mm/fadvise.c */
extern int vfs_fadvise(struct file *file, loff_t offset, loff_t len,
		       int advice);
extern int generic_fadvise(struct file *file, loff_t offset, loff_t len,
			   int advice);

#if defined(CONFIG_IO_URING)
extern struct sock *io_uring_get_socket(struct file *file);
#else
static inline struct sock *io_uring_get_socket(struct file *file)
{
	return NULL;
}
#endif

int vfs_ioc_setflags_prepare(struct iyesde *iyesde, unsigned int oldflags,
			     unsigned int flags);

int vfs_ioc_fssetxattr_check(struct iyesde *iyesde, const struct fsxattr *old_fa,
			     struct fsxattr *fa);

static inline void simple_fill_fsxattr(struct fsxattr *fa, __u32 xflags)
{
	memset(fa, 0, sizeof(*fa));
	fa->fsx_xflags = xflags;
}

/*
 * Flush file data before changing attributes.  Caller must hold any locks
 * required to prevent further writes to this file until we're done setting
 * flags.
 */
static inline int iyesde_drain_writes(struct iyesde *iyesde)
{
	iyesde_dio_wait(iyesde);
	return filemap_write_and_wait(iyesde->i_mapping);
}

#endif /* _LINUX_FS_H */
