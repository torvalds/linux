/*
 * common.h - Common definitions for both Kernel and user-mode utilities
 *
 * Copyright (C) 2005, 2006
 * Avishay Traeger (avishay@gmail.com) (avishay@il.ibm.com)
 * Copyright (C) 2005, 2006
 * International Business Machines
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

#ifndef __EXOFS_COM_H__
#define __EXOFS_COM_H__

#include <linux/types.h>

#include <scsi/osd_attributes.h>
#include <scsi/osd_initiator.h>
#include <scsi/osd_sec.h>

/****************************************************************************
 * Object ID related defines
 * NOTE: inode# = object ID - EXOFS_OBJ_OFF
 ****************************************************************************/
#define EXOFS_MIN_PID   0x10000	/* Smallest partition ID */
#define EXOFS_OBJ_OFF	0x10000	/* offset for objects */
#define EXOFS_SUPER_ID	0x10000	/* object ID for on-disk superblock */
#define EXOFS_ROOT_ID	0x10002	/* object ID for root directory */

/* exofs Application specific page/attribute */
# define EXOFS_APAGE_FS_DATA	(OSD_APAGE_APP_DEFINED_FIRST + 3)
# define EXOFS_ATTR_INODE_DATA	1

/*
 * The maximum number of files we can have is limited by the size of the
 * inode number.  This is the largest object ID that the file system supports.
 * Object IDs 0, 1, and 2 are always in use (see above defines).
 */
enum {
	EXOFS_MAX_INO_ID = (sizeof(ino_t) * 8 == 64) ? ULLONG_MAX :
					(1ULL << (sizeof(ino_t) * 8ULL - 1ULL)),
	EXOFS_MAX_ID	 = (EXOFS_MAX_INO_ID - 1 - EXOFS_OBJ_OFF),
};

/****************************************************************************
 * Misc.
 ****************************************************************************/
#define EXOFS_BLKSHIFT	12
#define EXOFS_BLKSIZE	(1UL << EXOFS_BLKSHIFT)

/****************************************************************************
 * superblock-related things
 ****************************************************************************/
#define EXOFS_SUPER_MAGIC	0x5DF5

/*
 * The file system control block - stored in an object's data (mainly, the one
 * with ID EXOFS_SUPER_ID).  This is where the in-memory superblock is stored
 * on disk.  Right now it just has a magic value, which is basically a sanity
 * check on our ability to communicate with the object store.
 */
struct exofs_fscb {
	__le64  s_nextid;	/* Highest object ID used */
	__le32  s_numfiles;	/* Number of files on fs */
	__le16  s_magic;	/* Magic signature */
	__le16  s_newfs;	/* Non-zero if this is a new fs */
};

/****************************************************************************
 * inode-related things
 ****************************************************************************/
#define EXOFS_IDATA		5

/*
 * The file control block - stored in an object's attributes.  This is where
 * the in-memory inode is stored on disk.
 */
struct exofs_fcb {
	__le64  i_size;			/* Size of the file */
	__le16  i_mode;         	/* File mode */
	__le16  i_links_count;  	/* Links count */
	__le32  i_uid;          	/* Owner Uid */
	__le32  i_gid;          	/* Group Id */
	__le32  i_atime;        	/* Access time */
	__le32  i_ctime;        	/* Creation time */
	__le32  i_mtime;        	/* Modification time */
	__le32  i_flags;        	/* File flags (unused for now)*/
	__le32  i_generation;   	/* File version (for NFS) */
	__le32  i_data[EXOFS_IDATA];	/* Short symlink names and device #s */
};

#define EXOFS_INO_ATTR_SIZE	sizeof(struct exofs_fcb)

/* This is the Attribute the fcb is stored in */
static const struct __weak osd_attr g_attr_inode_data = ATTR_DEF(
	EXOFS_APAGE_FS_DATA,
	EXOFS_ATTR_INODE_DATA,
	EXOFS_INO_ATTR_SIZE);

/****************************************************************************
 * dentry-related things
 ****************************************************************************/
#define EXOFS_NAME_LEN	255

/*
 * The on-disk directory entry
 */
struct exofs_dir_entry {
	__le64		inode_no;		/* inode number           */
	__le16		rec_len;		/* directory entry length */
	u8		name_len;		/* name length            */
	u8		file_type;		/* umm...file type        */
	char		name[EXOFS_NAME_LEN];	/* file name              */
};

enum {
	EXOFS_FT_UNKNOWN,
	EXOFS_FT_REG_FILE,
	EXOFS_FT_DIR,
	EXOFS_FT_CHRDEV,
	EXOFS_FT_BLKDEV,
	EXOFS_FT_FIFO,
	EXOFS_FT_SOCK,
	EXOFS_FT_SYMLINK,
	EXOFS_FT_MAX
};

#define EXOFS_DIR_PAD			4
#define EXOFS_DIR_ROUND			(EXOFS_DIR_PAD - 1)
#define EXOFS_DIR_REC_LEN(name_len) \
	(((name_len) + offsetof(struct exofs_dir_entry, name)  + \
	  EXOFS_DIR_ROUND) & ~EXOFS_DIR_ROUND)

/*************************
 * function declarations *
 *************************/
/* osd.c                 */
void exofs_make_credential(u8 cred_a[OSD_CAP_LEN],
			   const struct osd_obj_id *obj);

int exofs_check_ok_resid(struct osd_request *or, u64 *in_resid, u64 *out_resid);
static inline int exofs_check_ok(struct osd_request *or)
{
	return exofs_check_ok_resid(or, NULL, NULL);
}
int exofs_sync_op(struct osd_request *or, int timeout, u8 *cred);
int exofs_async_op(struct osd_request *or,
	osd_req_done_fn *async_done, void *caller_context, u8 *cred);

int extract_attr_from_req(struct osd_request *or, struct osd_attr *attr);

int osd_req_read_kern(struct osd_request *or,
	const struct osd_obj_id *obj, u64 offset, void *buff, u64 len);

int osd_req_write_kern(struct osd_request *or,
	const struct osd_obj_id *obj, u64 offset, void *buff, u64 len);

#endif /*ifndef __EXOFS_COM_H__*/
