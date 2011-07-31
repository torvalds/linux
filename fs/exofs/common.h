/*
 * common.h - Common definitions for both Kernel and user-mode utilities
 *
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
#define EXOFS_DEVTABLE_ID 0x10001 /* object ID for on-disk device table */
#define EXOFS_ROOT_ID	0x10002	/* object ID for root directory */

/* exofs Application specific page/attribute */
# define EXOFS_APAGE_FS_DATA	(OSD_APAGE_APP_DEFINED_FIRST + 3)
# define EXOFS_ATTR_INODE_DATA	1
# define EXOFS_ATTR_INODE_FILE_LAYOUT	2
# define EXOFS_ATTR_INODE_DIR_LAYOUT	3

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
 * The file system control block - stored in object EXOFS_SUPER_ID's data.
 * This is where the in-memory superblock is stored on disk.
 */
enum {EXOFS_FSCB_VER = 1, EXOFS_DT_VER = 1};
struct exofs_fscb {
	__le64  s_nextid;	/* Highest object ID used */
	__le64  s_numfiles;	/* Number of files on fs */
	__le32	s_version;	/* == EXOFS_FSCB_VER */
	__le16  s_magic;	/* Magic signature */
	__le16  s_newfs;	/* Non-zero if this is a new fs */

	/* From here on it's a static part, only written by mkexofs */
	__le64	s_dev_table_oid;   /* Resurved, not used */
	__le64	s_dev_table_count; /* == 0 means no dev_table */
} __packed;

/*
 * Describes the raid used in the FS. It is part of the device table.
 * This here is taken from the pNFS-objects definition. In exofs we
 * use one raid policy through-out the filesystem. (NOTE: the funny
 * alignment at begining. We take care of it at exofs_device_table.
 */
struct exofs_dt_data_map {
	__le32	cb_num_comps;
	__le64	cb_stripe_unit;
	__le32	cb_group_width;
	__le32	cb_group_depth;
	__le32	cb_mirror_cnt;
	__le32	cb_raid_algorithm;
} __packed;

/*
 * This is an osd device information descriptor. It is a single entry in
 * the exofs device table. It describes an osd target lun which
 * contains data belonging to this FS. (Same partition_id on all devices)
 */
struct exofs_dt_device_info {
	__le32	systemid_len;
	u8	systemid[OSD_SYSTEMID_LEN];
	__le64	long_name_offset;	/* If !0 then offset-in-file */
	__le32	osdname_len;		/* */
	u8	osdname[44];		/* Embbeded, Ususally an asci uuid */
} __packed;

/*
 * The EXOFS device table - stored in object EXOFS_DEVTABLE_ID's data.
 * It contains the raid used for this multy-device FS and an array of
 * participating devices.
 */
struct exofs_device_table {
	__le32				dt_version;	/* == EXOFS_DT_VER */
	struct exofs_dt_data_map	dt_data_map;	/* Raid policy to use */

	/* Resurved space For future use. Total includeing this:
	 * (8 * sizeof(le64))
	 */
	__le64				__Resurved[4];

	__le64				dt_num_devices;	/* Array size */
	struct exofs_dt_device_info	dt_dev_table[];	/* Array of devices */
} __packed;

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

/*
 * The on-disk (optional) layout structure.
 * sits in an EXOFS_ATTR_INODE_FILE_LAYOUT or EXOFS_ATTR_INODE_DIR_LAYOUT
 * attribute, attached to any inode, usually to a directory.
 */

enum exofs_inode_layout_gen_functions {
	LAYOUT_MOVING_WINDOW = 0,
	LAYOUT_IMPLICT = 1,
};

struct exofs_on_disk_inode_layout {
	__le16 gen_func; /* One of enum exofs_inode_layout_gen_functions */
	__le16 pad;
	union {
		/* gen_func == LAYOUT_MOVING_WINDOW (default) */
		struct exofs_layout_sliding_window {
			__le32 num_devices; /* first n devices in global-table*/
		} sliding_window __packed;

		/* gen_func == LAYOUT_IMPLICT */
		struct exofs_layout_implict_list {
			struct exofs_dt_data_map data_map;
			/* Variable array of size data_map.cb_num_comps. These
			 * are device indexes of the devices in the global table
			 */
			__le32 dev_indexes[];
		} implict __packed;
	};
} __packed;

static inline size_t exofs_on_disk_inode_layout_size(unsigned max_devs)
{
	return sizeof(struct exofs_on_disk_inode_layout) +
		max_devs * sizeof(__le32);
}

#endif /*ifndef __EXOFS_COM_H__*/
