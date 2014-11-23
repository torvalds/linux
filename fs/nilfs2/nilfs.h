/*
 * nilfs.h - NILFS local header file.
 *
 * Copyright (C) 2005-2008 Nippon Telegraph and Telephone Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Written by Koji Sato <koji@osrg.net>
 *            Ryusuke Konishi <ryusuke@osrg.net>
 */

#ifndef _NILFS_H
#define _NILFS_H

#include <linux/kernel.h>
#include <linux/buffer_head.h>
#include <linux/spinlock.h>
#include <linux/blkdev.h>
#include <linux/nilfs2_fs.h>
#include "the_nilfs.h"
#include "bmap.h"

/**
 * struct nilfs_inode_info - nilfs inode data in memory
 * @i_flags: inode flags
 * @i_state: dynamic state flags
 * @i_bmap: pointer on i_bmap_data
 * @i_bmap_data: raw block mapping
 * @i_xattr: <TODO>
 * @i_dir_start_lookup: page index of last successful search
 * @i_cno: checkpoint number for GC inode
 * @i_btnode_cache: cached pages of b-tree nodes
 * @i_dirty: list for connecting dirty files
 * @xattr_sem: semaphore for extended attributes processing
 * @i_bh: buffer contains disk inode
 * @i_root: root object of the current filesystem tree
 * @vfs_inode: VFS inode object
 */
struct nilfs_inode_info {
	__u32 i_flags;
	unsigned long  i_state;		/* Dynamic state flags */
	struct nilfs_bmap *i_bmap;
	struct nilfs_bmap i_bmap_data;
	__u64 i_xattr;	/* sector_t ??? */
	__u32 i_dir_start_lookup;
	__u64 i_cno;		/* check point number for GC inode */
	struct address_space i_btnode_cache;
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
	struct buffer_head *i_bh;	/* i_bh contains a new or dirty
					   disk inode */
	struct nilfs_root *i_root;
	struct inode vfs_inode;
};

static inline struct nilfs_inode_info *NILFS_I(const struct inode *inode)
{
	return container_of(inode, struct nilfs_inode_info, vfs_inode);
}

static inline struct nilfs_inode_info *
NILFS_BMAP_I(const struct nilfs_bmap *bmap)
{
	return container_of(bmap, struct nilfs_inode_info, i_bmap_data);
}

static inline struct inode *NILFS_BTNC_I(struct address_space *btnc)
{
	struct nilfs_inode_info *ii =
		container_of(btnc, struct nilfs_inode_info, i_btnode_cache);
	return &ii->vfs_inode;
}

/*
 * Dynamic state flags of NILFS on-memory inode (i_state)
 */
enum {
	NILFS_I_NEW = 0,		/* Inode is newly created */
	NILFS_I_DIRTY,			/* The file is dirty */
	NILFS_I_QUEUED,			/* inode is in dirty_files list */
	NILFS_I_BUSY,			/* inode is grabbed by a segment
					   constructor */
	NILFS_I_COLLECTED,		/* All dirty blocks are collected */
	NILFS_I_UPDATED,		/* The file has been written back */
	NILFS_I_INODE_SYNC,		/* dsync is not allowed for inode */
	NILFS_I_BMAP,			/* has bmap and btnode_cache */
	NILFS_I_GCINODE,		/* inode for GC, on memory only */
};

/*
 * commit flags for nilfs_commit_super and nilfs_sync_super
 */
enum {
	NILFS_SB_COMMIT = 0,	/* Commit a super block alternately */
	NILFS_SB_COMMIT_ALL	/* Commit both super blocks */
};

/*
 * Macros to check inode numbers
 */
#define NILFS_MDT_INO_BITS   \
	((unsigned int)(1 << NILFS_DAT_INO | 1 << NILFS_CPFILE_INO |	\
			1 << NILFS_SUFILE_INO | 1 << NILFS_IFILE_INO |	\
			1 << NILFS_ATIME_INO | 1 << NILFS_SKETCH_INO))

#define NILFS_SYS_INO_BITS   \
	((unsigned int)(1 << NILFS_ROOT_INO) | NILFS_MDT_INO_BITS)

#define NILFS_FIRST_INO(sb) (((struct the_nilfs *)sb->s_fs_info)->ns_first_ino)

#define NILFS_MDT_INODE(sb, ino) \
	((ino) < NILFS_FIRST_INO(sb) && (NILFS_MDT_INO_BITS & (1 << (ino))))
#define NILFS_VALID_INODE(sb, ino) \
	((ino) >= NILFS_FIRST_INO(sb) || (NILFS_SYS_INO_BITS & (1 << (ino))))

/**
 * struct nilfs_transaction_info: context information for synchronization
 * @ti_magic: Magic number
 * @ti_save: Backup of journal_info field of task_struct
 * @ti_flags: Flags
 * @ti_count: Nest level
 * @ti_garbage:	List of inode to be put when releasing semaphore
 */
struct nilfs_transaction_info {
	u32			ti_magic;
	void		       *ti_save;
				/* This should never used. If this happens,
				   one of other filesystems has a bug. */
	unsigned short		ti_flags;
	unsigned short		ti_count;
	struct list_head	ti_garbage;
};

/* ti_magic */
#define NILFS_TI_MAGIC		0xd9e392fb

/* ti_flags */
#define NILFS_TI_DYNAMIC_ALLOC	0x0001  /* Allocated from slab */
#define NILFS_TI_SYNC		0x0002	/* Force to construct segment at the
					   end of transaction. */
#define NILFS_TI_GC		0x0004	/* GC context */
#define NILFS_TI_COMMIT		0x0008	/* Change happened or not */
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
#error "NILFS: not yet supported POSIX ACL"
extern int nilfs_acl_chmod(struct inode *);
extern int nilfs_init_acl(struct inode *, struct inode *);
#else
static inline int nilfs_acl_chmod(struct inode *inode)
{
	return 0;
}

static inline int nilfs_init_acl(struct inode *inode, struct inode *dir)
{
	inode->i_mode &= ~current_umask();
	return 0;
}
#endif

#define NILFS_ATIME_DISABLE

/* Flags that should be inherited by new inodes from their parent. */
#define NILFS_FL_INHERITED						\
	(FS_SECRM_FL | FS_UNRM_FL | FS_COMPR_FL | FS_SYNC_FL |		\
	 FS_IMMUTABLE_FL | FS_APPEND_FL | FS_NODUMP_FL | FS_NOATIME_FL |\
	 FS_COMPRBLK_FL | FS_NOCOMP_FL | FS_NOTAIL_FL | FS_DIRSYNC_FL)

/* Mask out flags that are inappropriate for the given type of inode. */
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
extern int nilfs_add_link(struct dentry *, struct inode *);
extern ino_t nilfs_inode_by_name(struct inode *, const struct qstr *);
extern int nilfs_make_empty(struct inode *, struct inode *);
extern struct nilfs_dir_entry *
nilfs_find_entry(struct inode *, const struct qstr *, struct page **);
extern int nilfs_delete_entry(struct nilfs_dir_entry *, struct page *);
extern int nilfs_empty_dir(struct inode *);
extern struct nilfs_dir_entry *nilfs_dotdot(struct inode *, struct page **);
extern void nilfs_set_link(struct inode *, struct nilfs_dir_entry *,
			   struct page *, struct inode *);

/* file.c */
extern int nilfs_sync_file(struct file *, loff_t, loff_t, int);

/* ioctl.c */
long nilfs_ioctl(struct file *, unsigned int, unsigned long);
long nilfs_compat_ioctl(struct file *file, unsigned int cmd, unsigned long arg);
int nilfs_ioctl_prepare_clean_segments(struct the_nilfs *, struct nilfs_argv *,
				       void **);

/* inode.c */
void nilfs_inode_add_blocks(struct inode *inode, int n);
void nilfs_inode_sub_blocks(struct inode *inode, int n);
extern struct inode *nilfs_new_inode(struct inode *, umode_t);
extern void nilfs_free_inode(struct inode *);
extern int nilfs_get_block(struct inode *, sector_t, struct buffer_head *, int);
extern void nilfs_set_inode_flags(struct inode *);
extern int nilfs_read_inode_common(struct inode *, struct nilfs_inode *);
extern void nilfs_write_inode_common(struct inode *, struct nilfs_inode *, int);
struct inode *nilfs_ilookup(struct super_block *sb, struct nilfs_root *root,
			    unsigned long ino);
struct inode *nilfs_iget_locked(struct super_block *sb, struct nilfs_root *root,
				unsigned long ino);
struct inode *nilfs_iget(struct super_block *sb, struct nilfs_root *root,
			 unsigned long ino);
extern struct inode *nilfs_iget_for_gc(struct super_block *sb,
				       unsigned long ino, __u64 cno);
extern void nilfs_update_inode(struct inode *, struct buffer_head *, int);
extern void nilfs_truncate(struct inode *);
extern void nilfs_evict_inode(struct inode *);
extern int nilfs_setattr(struct dentry *, struct iattr *);
extern void nilfs_write_failed(struct address_space *mapping, loff_t to);
int nilfs_permission(struct inode *inode, int mask);
int nilfs_load_inode_block(struct inode *inode, struct buffer_head **pbh);
extern int nilfs_inode_dirty(struct inode *);
int nilfs_set_file_dirty(struct inode *inode, unsigned nr_dirty);
extern int __nilfs_mark_inode_dirty(struct inode *, int);
extern void nilfs_dirty_inode(struct inode *, int flags);
int nilfs_fiemap(struct inode *inode, struct fiemap_extent_info *fieinfo,
		 __u64 start, __u64 len);
static inline int nilfs_mark_inode_dirty(struct inode *inode)
{
	return __nilfs_mark_inode_dirty(inode, I_DIRTY);
}
static inline int nilfs_mark_inode_dirty_sync(struct inode *inode)
{
	return __nilfs_mark_inode_dirty(inode, I_DIRTY_SYNC);
}

/* super.c */
extern struct inode *nilfs_alloc_inode(struct super_block *);
extern void nilfs_destroy_inode(struct inode *);
extern __printf(3, 4)
void nilfs_error(struct super_block *, const char *, const char *, ...);
extern __printf(3, 4)
void nilfs_warning(struct super_block *, const char *, const char *, ...);
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
int nilfs_attach_checkpoint(struct super_block *sb, __u64 cno, int curr_mnt,
			    struct nilfs_root **root);
int nilfs_checkpoint_is_mounted(struct super_block *sb, __u64 cno);

/* gcinode.c */
int nilfs_gccache_submit_read_data(struct inode *, sector_t, sector_t, __u64,
				   struct buffer_head **);
int nilfs_gccache_submit_read_node(struct inode *, sector_t, __u64,
				   struct buffer_head **);
int nilfs_gccache_wait_and_mark_dirty(struct buffer_head *);
int nilfs_init_gcinode(struct inode *inode);
void nilfs_remove_all_gcinodes(struct the_nilfs *nilfs);

/* sysfs.c */
int __init nilfs_sysfs_init(void);
void nilfs_sysfs_exit(void);
int nilfs_sysfs_create_device_group(struct super_block *);
void nilfs_sysfs_delete_device_group(struct the_nilfs *);
int nilfs_sysfs_create_snapshot_group(struct nilfs_root *);
void nilfs_sysfs_delete_snapshot_group(struct nilfs_root *);

/*
 * Inodes and files operations
 */
extern const struct file_operations nilfs_dir_operations;
extern const struct inode_operations nilfs_file_inode_operations;
extern const struct file_operations nilfs_file_operations;
extern const struct address_space_operations nilfs_aops;
extern const struct inode_operations nilfs_dir_inode_operations;
extern const struct inode_operations nilfs_special_inode_operations;
extern const struct inode_operations nilfs_symlink_inode_operations;

/*
 * filesystem type
 */
extern struct file_system_type nilfs_fs_type;


#endif	/* _NILFS_H */
