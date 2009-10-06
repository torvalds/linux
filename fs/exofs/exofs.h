/*
 * Copyright (C) 2005, 2006
 * Avishay Traeger (avishay@gmail.com)
 * Copyright (C) 2008, 2009
 * Boaz Harrosh <bharrosh@panasas.com>
 *
 * Copyrights for code taken from ext2:
 *     Copyright (C) 1992, 1993, 1994, 1995
 *     Remy Card (card@masi.ibp.fr)
 *     Laboratoire MASI - Institut Blaise Pascal
 *     Universite Pierre et Marie Curie (Paris VI)
 *     from
 *     linux/fs/minix/inode.c
 *     Copyright (C) 1991, 1992  Linus Torvalds
 *
 * This file is part of exofs.
 *
 * exofs is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.  Since it is based on ext2, and the only
 * valid version of GPL for the Linux kernel is version 2, the only valid
 * version of GPL for exofs is version 2.
 *
 * exofs is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with exofs; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <linux/fs.h>
#include <linux/time.h>
#include "common.h"

#ifndef __EXOFS_H__
#define __EXOFS_H__

#define EXOFS_ERR(fmt, a...) printk(KERN_ERR "exofs: " fmt, ##a)

#ifdef CONFIG_EXOFS_DEBUG
#define EXOFS_DBGMSG(fmt, a...) \
	printk(KERN_NOTICE "exofs @%s:%d: " fmt, __func__, __LINE__, ##a)
#else
#define EXOFS_DBGMSG(fmt, a...) \
	do { if (0) printk(fmt, ##a); } while (0)
#endif

/* u64 has problems with printk this will cast it to unsigned long long */
#define _LLU(x) (unsigned long long)(x)

/*
 * our extension to the in-memory superblock
 */
struct exofs_sb_info {
	struct osd_dev	*s_dev;			/* returned by get_osd_dev    */
	osd_id		s_pid;			/* partition ID of file system*/
	int		s_timeout;		/* timeout for OSD operations */
	uint64_t	s_nextid;		/* highest object ID used     */
	uint32_t	s_numfiles;		/* number of files on fs      */
	spinlock_t	s_next_gen_lock;	/* spinlock for gen # update  */
	u32		s_next_generation;	/* next gen # to use          */
	atomic_t	s_curr_pending;		/* number of pending commands */
	uint8_t		s_cred[OSD_CAP_LEN];	/* all-powerful credential    */
};

/*
 * our extension to the in-memory inode
 */
struct exofs_i_info {
	unsigned long  i_flags;            /* various atomic flags            */
	uint32_t       i_data[EXOFS_IDATA];/*short symlink names and device #s*/
	uint32_t       i_dir_start_lookup; /* which page to start lookup      */
	wait_queue_head_t i_wq;            /* wait queue for inode            */
	uint64_t       i_commit_size;      /* the object's written length     */
	uint8_t        i_cred[OSD_CAP_LEN];/* all-powerful credential         */
	struct inode   vfs_inode;          /* normal in-memory inode          */
};

/*
 * our inode flags
 */
#define OBJ_2BCREATED	0	/* object will be created soon*/
#define OBJ_CREATED	1	/* object has been created on the osd*/

static inline int obj_2bcreated(struct exofs_i_info *oi)
{
	return test_bit(OBJ_2BCREATED, &oi->i_flags);
}

static inline void set_obj_2bcreated(struct exofs_i_info *oi)
{
	set_bit(OBJ_2BCREATED, &oi->i_flags);
}

static inline int obj_created(struct exofs_i_info *oi)
{
	return test_bit(OBJ_CREATED, &oi->i_flags);
}

static inline void set_obj_created(struct exofs_i_info *oi)
{
	set_bit(OBJ_CREATED, &oi->i_flags);
}

int __exofs_wait_obj_created(struct exofs_i_info *oi);
static inline int wait_obj_created(struct exofs_i_info *oi)
{
	if (likely(obj_created(oi)))
		return 0;

	return __exofs_wait_obj_created(oi);
}

/*
 * get to our inode from the vfs inode
 */
static inline struct exofs_i_info *exofs_i(struct inode *inode)
{
	return container_of(inode, struct exofs_i_info, vfs_inode);
}

/*
 * Maximum count of links to a file
 */
#define EXOFS_LINK_MAX           32000

/*************************
 * function declarations *
 *************************/
/* inode.c               */
void exofs_truncate(struct inode *inode);
int exofs_setattr(struct dentry *, struct iattr *);
int exofs_write_begin(struct file *file, struct address_space *mapping,
		loff_t pos, unsigned len, unsigned flags,
		struct page **pagep, void **fsdata);
extern struct inode *exofs_iget(struct super_block *, unsigned long);
struct inode *exofs_new_inode(struct inode *, int);
extern int exofs_write_inode(struct inode *, int);
extern void exofs_delete_inode(struct inode *);

/* dir.c:                */
int exofs_add_link(struct dentry *, struct inode *);
ino_t exofs_inode_by_name(struct inode *, struct dentry *);
int exofs_delete_entry(struct exofs_dir_entry *, struct page *);
int exofs_make_empty(struct inode *, struct inode *);
struct exofs_dir_entry *exofs_find_entry(struct inode *, struct dentry *,
					 struct page **);
int exofs_empty_dir(struct inode *);
struct exofs_dir_entry *exofs_dotdot(struct inode *, struct page **);
ino_t exofs_parent_ino(struct dentry *child);
int exofs_set_link(struct inode *, struct exofs_dir_entry *, struct page *,
		    struct inode *);

/* super.c               */
int exofs_sync_fs(struct super_block *sb, int wait);

/*********************
 * operation vectors *
 *********************/
/* dir.c:            */
extern const struct file_operations exofs_dir_operations;

/* file.c            */
extern const struct inode_operations exofs_file_inode_operations;
extern const struct file_operations exofs_file_operations;

/* inode.c           */
extern const struct address_space_operations exofs_aops;

/* namei.c           */
extern const struct inode_operations exofs_dir_inode_operations;
extern const struct inode_operations exofs_special_inode_operations;

/* symlink.c         */
extern const struct inode_operations exofs_symlink_inode_operations;
extern const struct inode_operations exofs_fast_symlink_inode_operations;

#endif
