/*
 *   Copyright (C) International Business Machines Corp., 2000-2001
 *
 *   This program is free software;  you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY;  without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 *   the GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program;  if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#ifndef _H_JFS_DINODE
#define _H_JFS_DINODE

/*
 *	jfs_dinode.h: on-disk inode manager
 */

#define INODESLOTSIZE		128
#define L2INODESLOTSIZE		7
#define log2INODESIZE		9	/* log2(bytes per dinode) */


/*
 *	on-disk inode : 512 bytes
 *
 * note: align 64-bit fields on 8-byte boundary.
 */
struct dinode {
	/*
	 *	I. base area (128 bytes)
	 *	------------------------
	 *
	 * define generic/POSIX attributes
	 */
	__le32 di_inostamp;	/* 4: stamp to show inode belongs to fileset */
	__le32 di_fileset;	/* 4: fileset number */
	__le32 di_number;	/* 4: inode number, aka file serial number */
	__le32 di_gen;		/* 4: inode generation number */

	pxd_t di_ixpxd;		/* 8: inode extent descriptor */

	__le64 di_size;		/* 8: size */
	__le64 di_nblocks;	/* 8: number of blocks allocated */

	__le32 di_nlink;	/* 4: number of links to the object */

	__le32 di_uid;		/* 4: user id of owner */
	__le32 di_gid;		/* 4: group id of owner */

	__le32 di_mode;		/* 4: attribute, format and permission */

	struct timestruc_t di_atime;	/* 8: time last data accessed */
	struct timestruc_t di_ctime;	/* 8: time last status changed */
	struct timestruc_t di_mtime;	/* 8: time last data modified */
	struct timestruc_t di_otime;	/* 8: time created */

	dxd_t di_acl;		/* 16: acl descriptor */

	dxd_t di_ea;		/* 16: ea descriptor */

	__le32 di_next_index;	/* 4: Next available dir_table index */

	__le32 di_acltype;	/* 4: Type of ACL */

	/*
	 *	Extension Areas.
	 *
	 *	Historically, the inode was partitioned into 4 128-byte areas,
	 *	the last 3 being defined as unions which could have multiple
	 *	uses.  The first 96 bytes had been completely unused until
	 *	an index table was added to the directory.  It is now more
	 *	useful to describe the last 3/4 of the inode as a single
	 *	union.  We would probably be better off redesigning the
	 *	entire structure from scratch, but we don't want to break
	 *	commonality with OS/2's JFS at this time.
	 */
	union {
		struct {
			/*
			 * This table contains the information needed to
			 * find a directory entry from a 32-bit index.
			 * If the index is small enough, the table is inline,
			 * otherwise, an x-tree root overlays this table
			 */
			struct dir_table_slot _table[12]; /* 96: inline */

			dtroot_t _dtroot;		/* 288: dtree root */
		} _dir;					/* (384) */
#define di_dirtable	u._dir._table
#define di_dtroot	u._dir._dtroot
#define di_parent	di_dtroot.header.idotdot
#define di_DASD		di_dtroot.header.DASD

		struct {
			union {
				u8 _data[96];		/* 96: unused */
				struct {
					void *_imap;	/* 4: unused */
					__le32 _gengen;	/* 4: generator */
				} _imap;
			} _u1;				/* 96: */
#define di_gengen	u._file._u1._imap._gengen

			union {
				xtpage_t _xtroot;
				struct {
					u8 unused[16];	/* 16: */
					dxd_t _dxd;	/* 16: */
					union {
						__le32 _rdev;	/* 4: */
						u8 _fastsymlink[128];
					} _u;
					u8 _inlineea[128];
				} _special;
			} _u2;
		} _file;
#define di_xtroot	u._file._u2._xtroot
#define di_dxd		u._file._u2._special._dxd
#define di_btroot	di_xtroot
#define di_inlinedata	u._file._u2._special._u
#define di_rdev		u._file._u2._special._u._rdev
#define di_fastsymlink	u._file._u2._special._u._fastsymlink
#define di_inlineea	u._file._u2._special._inlineea
	} u;
};

/* extended mode bits (on-disk inode di_mode) */
#define IFJOURNAL	0x00010000	/* journalled file */
#define ISPARSE		0x00020000	/* sparse file enabled */
#define INLINEEA	0x00040000	/* inline EA area free */
#define ISWAPFILE	0x00800000	/* file open for pager swap space */

/* more extended mode bits: attributes for OS/2 */
#define IREADONLY	0x02000000	/* no write access to file */
#define IHIDDEN		0x04000000	/* hidden file */
#define ISYSTEM		0x08000000	/* system file */

#define IDIRECTORY	0x20000000	/* directory (shadow of real bit) */
#define IARCHIVE	0x40000000	/* file archive bit */
#define INEWNAME	0x80000000	/* non-8.3 filename format */

#define IRASH		0x4E000000	/* mask for changeable attributes */
#define ATTRSHIFT	25	/* bits to shift to move attribute
				   specification to mode position */

/* extended attributes for Linux */

#define JFS_NOATIME_FL		0x00080000 /* do not update atime */

#define JFS_DIRSYNC_FL		0x00100000 /* dirsync behaviour */
#define JFS_SYNC_FL		0x00200000 /* Synchronous updates */
#define JFS_SECRM_FL		0x00400000 /* Secure deletion */
#define JFS_UNRM_FL		0x00800000 /* allow for undelete */

#define JFS_APPEND_FL		0x01000000 /* writes to file may only append */
#define JFS_IMMUTABLE_FL	0x02000000 /* Immutable file */

#define JFS_FL_USER_VISIBLE	0x03F80000
#define JFS_FL_USER_MODIFIABLE	0x03F80000
#define JFS_FL_INHERIT		0x03C80000

/* These are identical to EXT[23]_IOC_GETFLAGS/SETFLAGS */
#define JFS_IOC_GETFLAGS	_IOR('f', 1, long)
#define JFS_IOC_SETFLAGS	_IOW('f', 2, long)


#endif /*_H_JFS_DINODE */
