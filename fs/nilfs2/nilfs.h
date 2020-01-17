/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * nilfs.h - NILFS local header file.
 *
 * Copyright (C) 2005-2008 Nippon Telegraph and Telephone Corporation.
 *
 * Written by Koji Sato and Ryusuke Konishi.
 */

#ifndef _NILFS_H
#define _NILFS_H

#include <linux/kernel.h>
#include <linux/buffer_head.h>
#include <linux/spinlock.h>
#include <linux/blkdev.h>
#include <linux/nilfs2_api.h>
#include <linux/nilfs2_ondisk.h>
#include "the_nilfs.h"
#include "bmap.h"

/**
 * struct nilfs_iyesde_info - nilfs iyesde data in memory
 * @i_flags: iyesde flags
 * @i_state: dynamic state flags
 * @i_bmap: pointer on i_bmap_data
 * @i_bmap_data: raw block mapping
 * @i_xattr: <TODO>
 * @i_dir_start_lookup: page index of last successful search
 * @i_cyes: checkpoint number for GC iyesde
 * @i_btyesde_cache: cached pages of b-tree yesdes
 * @i_dirty: list for connecting dirty files
 * @xattr_sem: semaphore for extended attributes processing
 * @i_bh: buffer contains disk iyesde
 * @i_root: root object of the current filesystem tree
 * @vfs_iyesde: VFS iyesde object
 */
struct nilfs_iyesde_info {
	__u32 i_flags;
	unsigned long  i_state;		/* Dynamic state flags */
	struct nilfs_bmap *i_bmap;
	struct nilfs_bmap i_bmap_data;
	__u64 i_xattr;	/* sector_t ??? */
	__u32 i_dir_start_lookup;
	__u64 i_cyes;		/* check point number for GC iyesde */
	struct address_space i_btyesde_cache;
	struct list_head i_dirty;	/* List for connecting dirty files */

#ifdef CONFIG_NILFS_XATTR
	/*
	 * Extended attributes can be read independently of the main file
	 * data. Taking i_sem even when reading would cause contention
	 * between readers of EAs and writers of regular file data, so
	 * instead we synchronize on xattr_sem when reading or changing
	 * EAs.
	 */
	struct rw_semaphore xattr_sem;
#endif
	struct buffer_head *i_bh;	/*
					 * i_bh contains a new or dirty
					 * disk iyesde.
					 */
	struct nilfs_root *i_root;
	struct iyesde vfs_iyesde;
};

static inline struct nilfs_iyesde_info *NILFS_I(const struct iyesde *iyesde)
{
	return container_of(iyesde, struct nilfs_iyesde_info, vfs_iyesde);
}

static inline struct nilfs_iyesde_info *
NILFS_BMAP_I(const struct nilfs_bmap *bmap)
{
	return container_of(bmap, struct nilfs_iyesde_info, i_bmap_data);
}

static inline struct iyesde *NILFS_BTNC_I(struct address_space *btnc)
{
	struct nilfs_iyesde_info *ii =
		container_of(btnc, struct nilfs_iyesde_info, i_btyesde_cache);
	return &ii->vfs_iyesde;
}

/*
 * Dynamic state flags of NILFS on-memory iyesde (i_state)
 */
enum {
	NILFS_I_NEW = 0,		/* Iyesde is newly created */
	NILFS_I_DIRTY,			/* The file is dirty */
	NILFS_I_QUEUED,			/* iyesde is in dirty_files list */
	NILFS_I_BUSY,			/*
					 * Iyesde is grabbed by a segment
					 * constructor
					 */
	NILFS_I_COLLECTED,		/* All dirty blocks are collected */
	NILFS_I_UPDATED,		/* The file has been written back */
	NILFS_I_INODE_SYNC,		/* dsync is yest allowed for iyesde */
	NILFS_I_BMAP,			/* has bmap and btyesde_cache */
	NILFS_I_GCINODE,		/* iyesde for GC, on memory only */
};

/*
 * commit flags for nilfs_commit_super and nilfs_sync_super
 */
enum {
	NILFS_SB_COMMIT = 0,	/* Commit a super block alternately */
	NILFS_SB_COMMIT_ALL	/* Commit both super blocks */
};

/*
 * Macros to check iyesde numbers
 */
#define NILFS_MDT_INO_BITS						\
	(BIT(NILFS_DAT_INO) | BIT(NILFS_CPFILE_INO) |			\
	 BIT(NILFS_SUFILE_INO) | BIT(NILFS_IFILE_INO) |			\
	 BIT(NILFS_ATIME_INO) | BIT(NILFS_SKETCH_INO))

#define NILFS_SYS_INO_BITS (BIT(NILFS_ROOT_INO) | NILFS_MDT_INO_BITS)

#define NILFS_FIRST_INO(sb) (((struct the_nilfs *)sb->s_fs_info)->ns_first_iyes)

#define NILFS_MDT_INODE(sb, iyes) \
	((iyes) < NILFS_FIRST_INO(sb) && (NILFS_MDT_INO_BITS & BIT(iyes)))
#define NILFS_VALID_INODE(sb, iyes) \
	((iyes) >= NILFS_FIRST_INO(sb) || (NILFS_SYS_INO_BITS & BIT(iyes)))

/**
 * struct nilfs_transaction_info: context information for synchronization
 * @ti_magic: Magic number
 * @ti_save: Backup of journal_info field of task_struct
 * @ti_flags: Flags
 * @ti_count: Nest level
 */
struct nilfs_transaction_info {
	u32			ti_magic;
	void		       *ti_save;
				/*
				 * This should never be used.  If it happens,
				 * one of other filesystems has a bug.
				 */
	unsigned short		ti_flags;
	unsigned short		ti_count;
};

/* ti_magic */
#define NILFS_TI_MAGIC		0xd9e392fb

/* ti_flags */
#define NILFS_TI_DYNAMIC_ALLOC	0x0001  /* Allocated from slab */
#define NILFS_TI_SYNC		0x0002	/*
					 * Force to construct segment at the
					 * end of transaction.
					 */
#define NILFS_TI_GC		0x0004	/* GC context */
#define NILFS_TI_COMMIT		0x0008	/* Change happened or yest */
#define NILFS_TI_WRITER		0x0010	/* Constructor context */


int nilfs_transaction_begin(struct super_block *,
			    struct nilfs_transaction_info *, int);
int nilfs_transaction_commit(struct super_block *);
void nilfs_transaction_abort(struct super_block *);

static inline void nilfs_set_transaction_flag(unsigned int flag)
{
	struct nilfs_transaction_info *ti = current->journal_info;

	ti->ti_flags |= flag;
}

static inline int nilfs_test_transaction_flag(unsigned int flag)
{
	struct nilfs_transaction_info *ti = current->journal_info;

	if (ti == NULL || ti->ti_magic != NILFS_TI_MAGIC)
		return 0;
	return !!(ti->ti_flags & flag);
}

static inline int nilfs_doing_gc(void)
{
	return nilfs_test_transaction_flag(NILFS_TI_GC);
}

static inline int nilfs_doing_construction(void)
{
	return nilfs_test_transaction_flag(NILFS_TI_WRITER);
}

/*
 * function prototype
 */
#ifdef CONFIG_NILFS_POSIX_ACL
#error "NILFS: yest yet supported POSIX ACL"
extern int nilfs_acl_chmod(struct iyesde *);
extern int nilfs_init_acl(struct iyesde *, struct iyesde *);
#else
static inline int nilfs_acl_chmod(struct iyesde *iyesde)
{
	return 0;
}

static inline int nilfs_init_acl(struct iyesde *iyesde, struct iyesde *dir)
{
	iyesde->i_mode &= ~current_umask();
	return 0;
}
#endif

#define NILFS_ATIME_DISABLE

/* Flags that should be inherited by new iyesdes from their parent. */
#define NILFS_FL_INHERITED						\
	(FS_SECRM_FL | FS_UNRM_FL | FS_COMPR_FL | FS_SYNC_FL |		\
	 FS_IMMUTABLE_FL | FS_APPEND_FL | FS_NODUMP_FL | FS_NOATIME_FL |\
	 FS_COMPRBLK_FL | FS_NOCOMP_FL | FS_NOTAIL_FL | FS_DIRSYNC_FL)

/* Mask out flags that are inappropriate for the given type of iyesde. */
static inline __u32 nilfs_mask_flags(umode_t mode, __u32 flags)
{
	if (S_ISDIR(mode))
		return flags;
	else if (S_ISREG(mode))
		return flags & ~(FS_DIRSYNC_FL | FS_TOPDIR_FL);
	else
		return flags & (FS_NODUMP_FL | FS_NOATIME_FL);
}

/* dir.c */
extern int nilfs_add_link(struct dentry *, struct iyesde *);
extern iyes_t nilfs_iyesde_by_name(struct iyesde *, const struct qstr *);
extern int nilfs_make_empty(struct iyesde *, struct iyesde *);
extern struct nilfs_dir_entry *
nilfs_find_entry(struct iyesde *, const struct qstr *, struct page **);
extern int nilfs_delete_entry(struct nilfs_dir_entry *, struct page *);
extern int nilfs_empty_dir(struct iyesde *);
extern struct nilfs_dir_entry *nilfs_dotdot(struct iyesde *, struct page **);
extern void nilfs_set_link(struct iyesde *, struct nilfs_dir_entry *,
			   struct page *, struct iyesde *);

/* file.c */
extern int nilfs_sync_file(struct file *, loff_t, loff_t, int);

/* ioctl.c */
long nilfs_ioctl(struct file *, unsigned int, unsigned long);
long nilfs_compat_ioctl(struct file *file, unsigned int cmd, unsigned long arg);
int nilfs_ioctl_prepare_clean_segments(struct the_nilfs *, struct nilfs_argv *,
				       void **);

/* iyesde.c */
void nilfs_iyesde_add_blocks(struct iyesde *iyesde, int n);
void nilfs_iyesde_sub_blocks(struct iyesde *iyesde, int n);
extern struct iyesde *nilfs_new_iyesde(struct iyesde *, umode_t);
extern int nilfs_get_block(struct iyesde *, sector_t, struct buffer_head *, int);
extern void nilfs_set_iyesde_flags(struct iyesde *);
extern int nilfs_read_iyesde_common(struct iyesde *, struct nilfs_iyesde *);
extern void nilfs_write_iyesde_common(struct iyesde *, struct nilfs_iyesde *, int);
struct iyesde *nilfs_ilookup(struct super_block *sb, struct nilfs_root *root,
			    unsigned long iyes);
struct iyesde *nilfs_iget_locked(struct super_block *sb, struct nilfs_root *root,
				unsigned long iyes);
struct iyesde *nilfs_iget(struct super_block *sb, struct nilfs_root *root,
			 unsigned long iyes);
extern struct iyesde *nilfs_iget_for_gc(struct super_block *sb,
				       unsigned long iyes, __u64 cyes);
extern void nilfs_update_iyesde(struct iyesde *, struct buffer_head *, int);
extern void nilfs_truncate(struct iyesde *);
extern void nilfs_evict_iyesde(struct iyesde *);
extern int nilfs_setattr(struct dentry *, struct iattr *);
extern void nilfs_write_failed(struct address_space *mapping, loff_t to);
int nilfs_permission(struct iyesde *iyesde, int mask);
int nilfs_load_iyesde_block(struct iyesde *iyesde, struct buffer_head **pbh);
extern int nilfs_iyesde_dirty(struct iyesde *);
int nilfs_set_file_dirty(struct iyesde *iyesde, unsigned int nr_dirty);
extern int __nilfs_mark_iyesde_dirty(struct iyesde *, int);
extern void nilfs_dirty_iyesde(struct iyesde *, int flags);
int nilfs_fiemap(struct iyesde *iyesde, struct fiemap_extent_info *fieinfo,
		 __u64 start, __u64 len);
static inline int nilfs_mark_iyesde_dirty(struct iyesde *iyesde)
{
	return __nilfs_mark_iyesde_dirty(iyesde, I_DIRTY);
}
static inline int nilfs_mark_iyesde_dirty_sync(struct iyesde *iyesde)
{
	return __nilfs_mark_iyesde_dirty(iyesde, I_DIRTY_SYNC);
}

/* super.c */
extern struct iyesde *nilfs_alloc_iyesde(struct super_block *);

extern __printf(3, 4)
void __nilfs_msg(struct super_block *sb, const char *level,
		 const char *fmt, ...);
extern __printf(3, 4)
void __nilfs_error(struct super_block *sb, const char *function,
		   const char *fmt, ...);

#ifdef CONFIG_PRINTK

#define nilfs_msg(sb, level, fmt, ...)					\
	__nilfs_msg(sb, level, fmt, ##__VA_ARGS__)
#define nilfs_error(sb, fmt, ...)					\
	__nilfs_error(sb, __func__, fmt, ##__VA_ARGS__)

#else

#define nilfs_msg(sb, level, fmt, ...)					\
	do {								\
		yes_printk(fmt, ##__VA_ARGS__);				\
		(void)(sb);						\
	} while (0)
#define nilfs_error(sb, fmt, ...)					\
	do {								\
		yes_printk(fmt, ##__VA_ARGS__);				\
		__nilfs_error(sb, "", " ");				\
	} while (0)

#endif /* CONFIG_PRINTK */

extern struct nilfs_super_block *
nilfs_read_super_block(struct super_block *, u64, int, struct buffer_head **);
extern int nilfs_store_magic_and_option(struct super_block *,
					struct nilfs_super_block *, char *);
extern int nilfs_check_feature_compatibility(struct super_block *,
					     struct nilfs_super_block *);
extern void nilfs_set_log_cursor(struct nilfs_super_block *,
				 struct the_nilfs *);
struct nilfs_super_block **nilfs_prepare_super(struct super_block *sb,
					       int flip);
int nilfs_commit_super(struct super_block *sb, int flag);
int nilfs_cleanup_super(struct super_block *sb);
int nilfs_resize_fs(struct super_block *sb, __u64 newsize);
int nilfs_attach_checkpoint(struct super_block *sb, __u64 cyes, int curr_mnt,
			    struct nilfs_root **root);
int nilfs_checkpoint_is_mounted(struct super_block *sb, __u64 cyes);

/* gciyesde.c */
int nilfs_gccache_submit_read_data(struct iyesde *, sector_t, sector_t, __u64,
				   struct buffer_head **);
int nilfs_gccache_submit_read_yesde(struct iyesde *, sector_t, __u64,
				   struct buffer_head **);
int nilfs_gccache_wait_and_mark_dirty(struct buffer_head *);
int nilfs_init_gciyesde(struct iyesde *iyesde);
void nilfs_remove_all_gciyesdes(struct the_nilfs *nilfs);

/* sysfs.c */
int __init nilfs_sysfs_init(void);
void nilfs_sysfs_exit(void);
int nilfs_sysfs_create_device_group(struct super_block *);
void nilfs_sysfs_delete_device_group(struct the_nilfs *);
int nilfs_sysfs_create_snapshot_group(struct nilfs_root *);
void nilfs_sysfs_delete_snapshot_group(struct nilfs_root *);

/*
 * Iyesdes and files operations
 */
extern const struct file_operations nilfs_dir_operations;
extern const struct iyesde_operations nilfs_file_iyesde_operations;
extern const struct file_operations nilfs_file_operations;
extern const struct address_space_operations nilfs_aops;
extern const struct iyesde_operations nilfs_dir_iyesde_operations;
extern const struct iyesde_operations nilfs_special_iyesde_operations;
extern const struct iyesde_operations nilfs_symlink_iyesde_operations;

/*
 * filesystem type
 */
extern struct file_system_type nilfs_fs_type;


#endif	/* _NILFS_H */
